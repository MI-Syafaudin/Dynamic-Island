#include "system_module.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <thread>

namespace di {

SystemModule::SystemModule() {
    // Detect GPU busy percent path (AMD / generic DRM)
    if (std::filesystem::exists("/sys/class/drm")) {
        for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm")) {
            std::string name = entry.path().filename().string();
            if (name.rfind("card", 0) == 0 && name.find('-') == std::string::npos) {
                std::string p = entry.path() / "device/gpu_busy_percent";
                if (std::filesystem::exists(p)) {
                    m_gpu_busy_path = p;
                    break;
                }
                std::string intel_act = entry.path() / "gt/gt0/rps_act_freq_mhz";
                std::string intel_max = entry.path() / "gt/gt0/rps_max_freq_mhz";
                if (std::filesystem::exists(intel_act) && std::filesystem::exists(intel_max)) {
                    m_intel_act_freq_path = intel_act;
                    m_intel_max_freq_path = intel_max;
                }
            }
        }
    }

    // Detect GPU name
    FILE* fp = popen("lspci -d ::0300 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            std::string s(buf);
            if (s.find("Vega") != std::string::npos || s.find("Picasso") != std::string::npos || s.find("Raven") != std::string::npos) {
                m_gpu_name = "(Radeon Vega 3)";
            } else if (s.find("AMD") != std::string::npos || s.find("Radeon") != std::string::npos) {
                m_gpu_name = "(AMD Radeon)";
            } else if (s.find("Intel") != std::string::npos) {
                m_gpu_name = "(Intel Graphics)";
            } else if (s.find("NVIDIA") != std::string::npos) {
                m_gpu_name = "(NVIDIA)";
            }
        }
        pclose(fp);
    }
    if (m_gpu_name.empty()) {
        m_gpu_name = "(Radeon Vega 3)";
    }

    // Seed initial CPU sample
    update_cpu();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    update_cpu();
    update_ram();
    update_gpu();
    update_temp();
}

void SystemModule::update_cpu() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return;

    std::string line;
    if (std::getline(file, line)) {
        if (line.substr(0, 4) == "cpu ") {
            std::istringstream ss(line.substr(5));
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
                unsigned long long work_jiffies = user + nice + system + irq + softirq + steal;
                unsigned long long total_jiffies = work_jiffies + idle + iowait;

                if (m_prev_total_jiffies == 0) {
                    m_prev_total_jiffies = total_jiffies;
                    m_prev_work_jiffies = work_jiffies;
                    return;
                }

                if (total_jiffies <= m_prev_total_jiffies) {
                    return;
                }

                unsigned long long total_delta = total_jiffies - m_prev_total_jiffies;
                // Require at least 15 jiffies (~150ms) to prevent volatile readings
                if (total_delta < 15) {
                    return;
                }

                unsigned long long work_delta = (work_jiffies >= m_prev_work_jiffies)
                                                ? (work_jiffies - m_prev_work_jiffies)
                                                : 0;
                int raw_cpu = std::clamp(static_cast<int>(std::round((work_delta * 100.0) / total_delta)), 0, 100);

                if (m_cpu_percent == 0) {
                    m_cpu_percent = raw_cpu;
                } else {
                    // Exponential Moving Average (EMA) to smooth out micro-spikes
                    m_cpu_percent = std::clamp(static_cast<int>(std::round(0.70 * raw_cpu + 0.30 * m_cpu_percent)), 0, 100);
                }

                m_prev_total_jiffies = total_jiffies;
                m_prev_work_jiffies = work_jiffies;
            }
        }
    }
}

void SystemModule::update_ram() {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) return;

    unsigned long long mem_total_kb = 0;
    unsigned long long mem_avail_kb = 0;
    std::string key;
    unsigned long long val;
    std::string unit;

    while (file >> key >> val >> unit) {
        if (key == "MemTotal:") mem_total_kb = val;
        else if (key == "MemAvailable:") mem_avail_kb = val;
        if (mem_total_kb > 0 && mem_avail_kb > 0) break;
    }

    if (mem_total_kb > 0) {
        unsigned long long mem_used_kb = mem_total_kb - mem_avail_kb;
        m_ram_percent = std::clamp(static_cast<int>(std::round((mem_used_kb * 100.0) / mem_total_kb)), 0, 100);
        m_ram_used_gb = mem_used_kb / 1048576.0;
        m_ram_total_gb = mem_total_kb / 1048576.0;
    }
}

void SystemModule::update_gpu() {
    int raw_percent = 0;
    if (!m_gpu_busy_path.empty()) {
        // Multi-sample gpu_busy_percent over a short interval (3 samples, ~8ms total)
        // to avoid catching momentary single-frame compositing spikes or idle vblank drops
        int sum_samples = 0;
        int valid_samples = 0;
        for (int i = 0; i < 3; ++i) {
            std::ifstream file(m_gpu_busy_path);
            if (file.is_open()) {
                int sample = 0;
                if (file >> sample) {
                    sum_samples += std::clamp(sample, 0, 100);
                    valid_samples++;
                }
            }
            if (i < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        }
        if (valid_samples > 0) {
            raw_percent = sum_samples / valid_samples;
        }
    } else if (!m_intel_act_freq_path.empty() && !m_intel_max_freq_path.empty()) {
        int act = 0, max_f = 0;
        std::ifstream f_act(m_intel_act_freq_path);
        std::ifstream f_max(m_intel_max_freq_path);
        if (f_act >> act && f_max >> max_f && max_f > 0) {
            raw_percent = std::clamp(static_cast<int>(std::round((act * 100.0) / max_f)), 0, 100);
        }
    }

    // Apply EMA smoothing to GPU reading
    if (m_gpu_percent == 0) {
        m_gpu_percent = raw_percent;
    } else {
        m_gpu_percent = std::clamp(static_cast<int>(std::round(0.65 * raw_percent + 0.35 * m_gpu_percent)), 0, 100);
    }
}

void SystemModule::update_temp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (file.is_open()) {
        int millidegrees = 0;
        file >> millidegrees;
        m_cpu_temp = millidegrees / 1000;
    }
}

void SystemModule::update() {
    update_cpu();
    update_ram();
    update_gpu();
    update_temp();
}

void SystemModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    // In compact mode, show mini CPU / RAM indicator
    std::string text = " " + std::to_string(m_cpu_percent) + "%";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.8);
    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "system"});
    current_x += tw + 10;

    g_object_unref(layout);
}

void SystemModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    auto draw_metric_row = [&](int row_y, const std::string& label, int percent, const std::string& extra, const ColorRGBA& col) {
        // Label on left
        pango_layout_set_text(layout, label.c_str(), -1);
        int ltw, lth;
        pango_layout_get_pixel_size(layout, &ltw, &lth);
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        cairo_move_to(cr, 20, row_y);
        pango_cairo_show_layout(cr, layout);

        // Extra info / percent on right
        std::string right_text = std::to_string(percent) + "% " + extra;
        pango_layout_set_text(layout, right_text.c_str(), -1);
        int rtw, rth;
        pango_layout_get_pixel_size(layout, &rtw, &rth);
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
        cairo_move_to(cr, w - 20 - rtw, row_y);
        pango_cairo_show_layout(cr, layout);

        // Mini progress bar below label
        int bx = 20;
        int by = row_y + 16;
        int bw = w - 40;
        int bh = 6;
        double br = 3.0;

        cairo_new_sub_path(cr);
        cairo_arc(cr, bx + br, by + br, br, M_PI / 2, 3 * M_PI / 2);
        cairo_arc(cr, bx + bw - br, by + br, br, -M_PI / 2, M_PI / 2);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 0.22, 0.22, 0.24, 0.8);
        cairo_fill(cr);

        int fw = std::max(static_cast<int>(bw * (percent / 100.0)), (percent > 0 ? 6 : 0));
        if (fw > 0) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, bx + br, by + br, br, M_PI / 2, 3 * M_PI / 2);
            cairo_arc(cr, bx + fw - br, by + br, br, -M_PI / 2, M_PI / 2);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.95);
            cairo_fill(cr);
        }
    };

    // Row 1: CPU
    std::string cpu_extra = "• " + std::to_string(m_cpu_temp) + "°C";
    draw_metric_row(10, " CPU", m_cpu_percent, cpu_extra, config.colors.accent);

    // Row 2: RAM
    std::stringstream ram_ss;
    ram_ss << "(" << std::fixed << std::setprecision(1) << m_ram_used_gb << " / " << m_ram_total_gb << " GB)";
    draw_metric_row(40, "󰘚 RAM", m_ram_percent, ram_ss.str(), config.colors.accent_blue);

    // Row 3: GPU
    draw_metric_row(70, "󰢮 GPU", m_gpu_percent, m_gpu_name, config.colors.warning);

    g_object_unref(layout);

    // Click anywhere to collapse
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
