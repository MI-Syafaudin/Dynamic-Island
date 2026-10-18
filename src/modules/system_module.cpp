#include "system_module.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace di {

SystemModule::SystemModule() {
    m_fd_stat = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    m_fd_meminfo = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
    m_fd_temp = open(m_temp_path.c_str(), O_RDONLY | O_CLOEXEC);

    detect_gpu_sysfs();

    // Check if swap is zram
    m_is_zram = false;
    if (std::filesystem::exists("/sys/block/zram0")) {
        m_is_zram = true;
    } else {
        std::ifstream swaps("/proc/swaps");
        if (swaps.is_open()) {
            std::string s_line;
            while (std::getline(swaps, s_line)) {
                if (s_line.find("zram") != std::string::npos) {
                    m_is_zram = true;
                    break;
                }
            }
        }
    }

    // Seed initial CPU sample
    update_cpu();
    update_ram();
    update_gpu();
    update_temp();
}

void SystemModule::detect_gpu_sysfs() {
    // 1. Detect GPU busy percent / intel frequency paths via sysfs
    if (std::filesystem::exists("/sys/class/drm")) {
        for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm")) {
            std::string name = entry.path().filename().string();
            if (name.rfind("card", 0) == 0 && name.find('-') == std::string::npos) {
                std::string p = entry.path() / "device/gpu_busy_percent";
                if (std::filesystem::exists(p)) {
                    m_gpu_busy_path = p;
                    m_fd_gpu = open(p.c_str(), O_RDONLY | O_CLOEXEC);
                }
                std::string intel_act = entry.path() / "gt/gt0/rps_act_freq_mhz";
                std::string intel_max = entry.path() / "gt/gt0/rps_max_freq_mhz";
                if (std::filesystem::exists(intel_act) && std::filesystem::exists(intel_max)) {
                    m_intel_act_freq_path = intel_act;
                    m_intel_max_freq_path = intel_max;
                    m_fd_intel_act = open(intel_act.c_str(), O_RDONLY | O_CLOEXEC);
                    m_fd_intel_max = open(intel_max.c_str(), O_RDONLY | O_CLOEXEC);
                }

                // Zero-subprocess GPU vendor/model detection from sysfs
                std::string vendor_path = entry.path() / "device/vendor";
                std::string device_path = entry.path() / "device/device";
                std::ifstream vf(vendor_path);
                std::ifstream df(device_path);
                std::string v_str, d_str;
                if (vf >> v_str) {
                    if (v_str.find("0x1002") != std::string::npos) { // AMD
                        if (df >> d_str) {
                            if (d_str.find("15d8") != std::string::npos || d_str.find("15D8") != std::string::npos) {
                                m_gpu_name = "(Radeon Vega 3)";
                            } else {
                                m_gpu_name = "(AMD Radeon)";
                            }
                        } else {
                            m_gpu_name = "(AMD Radeon)";
                        }
                    } else if (v_str.find("0x8086") != std::string::npos) { // Intel
                        m_gpu_name = "(Intel Graphics)";
                    } else if (v_str.find("0x10de") != std::string::npos) { // NVIDIA
                        m_gpu_name = "(NVIDIA)";
                    }
                }

                if (!m_gpu_busy_path.empty() || !m_intel_act_freq_path.empty()) {
                    break;
                }
            }
        }
    }

    if (m_gpu_name.empty()) {
        m_gpu_name = "(Radeon Vega 3)";
    }
}

void SystemModule::configure(double cpu_alpha, double gpu_alpha, const std::string& gpu_name_override, const std::string& temp_path_override) {
    m_cpu_smoothing_alpha = std::clamp(cpu_alpha, 0.05, 1.0);
    m_gpu_smoothing_alpha = std::clamp(gpu_alpha, 0.05, 1.0);

    if (!gpu_name_override.empty()) {
        m_gpu_name = gpu_name_override;
    }

    if (!temp_path_override.empty() && temp_path_override != m_temp_path) {
        m_temp_path = temp_path_override;
        if (m_fd_temp >= 0) {
            close(m_fd_temp);
            m_fd_temp = -1;
        }
        m_fd_temp = open(m_temp_path.c_str(), O_RDONLY | O_CLOEXEC);
    }
}

SystemModule::~SystemModule() {
    if (m_fd_stat >= 0) {
        close(m_fd_stat);
        m_fd_stat = -1;
    }
    if (m_fd_meminfo >= 0) {
        close(m_fd_meminfo);
        m_fd_meminfo = -1;
    }
    if (m_fd_temp >= 0) {
        close(m_fd_temp);
        m_fd_temp = -1;
    }
    if (m_fd_gpu >= 0) {
        close(m_fd_gpu);
        m_fd_gpu = -1;
    }
    if (m_fd_intel_act >= 0) {
        close(m_fd_intel_act);
        m_fd_intel_act = -1;
    }
    if (m_fd_intel_max >= 0) {
        close(m_fd_intel_max);
        m_fd_intel_max = -1;
    }
}

void SystemModule::update_cpu() {
    if (m_fd_stat < 0) {
        m_fd_stat = open("/proc/stat", O_RDONLY | O_CLOEXEC);
        if (m_fd_stat < 0) return;
    }

    char buf[1024];
    ssize_t bytes_read = pread(m_fd_stat, buf, sizeof(buf) - 1, 0);
    if (bytes_read <= 0) return;
    buf[bytes_read] = '\0';

    char* line = buf;
    char* eol = strchr(buf, '\n');
    if (eol) *eol = '\0';

    if (strncmp(line, "cpu ", 4) == 0) {
        unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
            unsigned long long work_jiffies = user + nice + system + irq + softirq + steal;
            unsigned long long total_jiffies = work_jiffies + idle + iowait;

            if (m_prev_total_jiffies == 0) {
                m_prev_total_jiffies = total_jiffies;
                m_prev_work_jiffies = work_jiffies;
                return;
            }

            if (total_jiffies > m_prev_total_jiffies) {
                unsigned long long total_delta = total_jiffies - m_prev_total_jiffies;
                // Fast-sampling threshold (>= 2 jiffies) for 200ms-250ms responsiveness
                if (total_delta >= 2) {
                    unsigned long long work_delta = (work_jiffies >= m_prev_work_jiffies)
                                                    ? (work_jiffies - m_prev_work_jiffies)
                                                    : 0;
                    int raw_cpu = std::clamp(static_cast<int>(std::round((work_delta * 100.0) / total_delta)), 0, 100);

                    if (m_cpu_percent == 0) {
                        m_cpu_percent = raw_cpu;
                    } else {
                        m_cpu_percent = std::clamp(static_cast<int>(std::round(m_cpu_smoothing_alpha * raw_cpu + (1.0 - m_cpu_smoothing_alpha) * m_cpu_percent)), 0, 100);
                    }

                    m_prev_total_jiffies = total_jiffies;
                    m_prev_work_jiffies = work_jiffies;
                }
            }
        }
    }
}

void SystemModule::update_ram() {
    if (m_fd_meminfo < 0) {
        m_fd_meminfo = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
        if (m_fd_meminfo < 0) return;
    }

    char buf[2048];
    ssize_t bytes_read = pread(m_fd_meminfo, buf, sizeof(buf) - 1, 0);
    if (bytes_read <= 0) return;
    buf[bytes_read] = '\0';

    unsigned long long mem_total_kb = 0;
    unsigned long long mem_avail_kb = 0;
    unsigned long long swap_total_kb = 0;
    unsigned long long swap_free_kb = 0;

    char* cur = buf;
    while (cur && *cur) {
        char* next = strchr(cur, '\n');
        if (next) *next = '\0';

        if (strncmp(cur, "MemTotal:", 9) == 0) sscanf(cur + 9, "%llu", &mem_total_kb);
        else if (strncmp(cur, "MemAvailable:", 13) == 0) sscanf(cur + 13, "%llu", &mem_avail_kb);
        else if (strncmp(cur, "SwapTotal:", 10) == 0) sscanf(cur + 10, "%llu", &swap_total_kb);
        else if (strncmp(cur, "SwapFree:", 9) == 0) sscanf(cur + 9, "%llu", &swap_free_kb);

        if (mem_total_kb > 0 && mem_avail_kb > 0 && swap_total_kb > 0 && swap_free_kb > 0) break;

        if (next) cur = next + 1;
        else break;
    }

    if (mem_total_kb > 0) {
        unsigned long long mem_used_kb = (mem_total_kb >= mem_avail_kb) ? (mem_total_kb - mem_avail_kb) : 0;
        m_ram_percent = std::clamp(static_cast<int>(std::round((mem_used_kb * 100.0) / mem_total_kb)), 0, 100);
        m_ram_used_gb = mem_used_kb / 1048576.0;
        m_ram_total_gb = mem_total_kb / 1048576.0;
    }

    if (swap_total_kb > 0) {
        unsigned long long swap_used_kb = (swap_total_kb >= swap_free_kb) ? (swap_total_kb - swap_free_kb) : 0;
        m_swap_percent = std::clamp(static_cast<int>(std::round((swap_used_kb * 100.0) / swap_total_kb)), 0, 100);
        m_swap_used_gb = swap_used_kb / 1048576.0;
        m_swap_total_gb = swap_total_kb / 1048576.0;
    } else {
        m_swap_percent = 0;
        m_swap_used_gb = 0.0;
        m_swap_total_gb = 0.0;
    }
}

void SystemModule::update_gpu() {
    int raw_percent = 0;

    if (m_fd_gpu >= 0) {
        char buf[32];
        ssize_t n = pread(m_fd_gpu, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            int sample = 0;
            if (sscanf(buf, "%d", &sample) == 1) {
                raw_percent = std::clamp(sample, 0, 100);
            }
        }
    } else if (m_fd_intel_act >= 0 && m_fd_intel_max >= 0) {
        char buf_act[32];
        char buf_max[32];
        ssize_t n_act = pread(m_fd_intel_act, buf_act, sizeof(buf_act) - 1, 0);
        ssize_t n_max = pread(m_fd_intel_max, buf_max, sizeof(buf_max) - 1, 0);
        if (n_act > 0 && n_max > 0) {
            buf_act[n_act] = '\0';
            buf_max[n_max] = '\0';
            int act = 0, max_f = 0;
            if (sscanf(buf_act, "%d", &act) == 1 && sscanf(buf_max, "%d", &max_f) == 1 && max_f > 0) {
                raw_percent = std::clamp(static_cast<int>(std::round((act * 100.0) / max_f)), 0, 100);
            }
        }
    }

    // Apply EMA smoothing to GPU reading
    if (m_gpu_percent == 0) {
        m_gpu_percent = raw_percent;
    } else {
        m_gpu_percent = std::clamp(static_cast<int>(std::round(m_gpu_smoothing_alpha * raw_percent + (1.0 - m_gpu_smoothing_alpha) * m_gpu_percent)), 0, 100);
    }
}

void SystemModule::update_temp() {
    if (m_fd_temp < 0) {
        m_fd_temp = open(m_temp_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (m_fd_temp < 0) return;
    }

    char buf[64];
    ssize_t bytes_read = pread(m_fd_temp, buf, sizeof(buf) - 1, 0);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        int millidegrees = 0;
        if (sscanf(buf, "%d", &millidegrees) == 1) {
            m_cpu_temp = millidegrees / 1000;
        }
    }
}

void SystemModule::update(bool full) {
    update_cpu();
    if (full) {
        update_ram();
        update_gpu();
        update_temp();
    }
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
        (void)bh;
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
    draw_metric_row(12, " CPU", m_cpu_percent, cpu_extra, config.colors.accent);

    // Row 2: RAM
    std::stringstream ram_ss;
    ram_ss << "(" << std::fixed << std::setprecision(1) << m_ram_used_gb << " / " << m_ram_total_gb << " GB)";
    draw_metric_row(42, "󰘚 RAM", m_ram_percent, ram_ss.str(), config.colors.accent_blue);

    // Row 3: ZRAM / SWAP
    std::string swap_label = m_is_zram ? "󰾴 ZRAM" : "󰾴 SWAP";
    std::stringstream swap_ss;
    if (m_swap_total_gb > 0) {
        swap_ss << "(" << std::fixed << std::setprecision(1) << m_swap_used_gb << " / " << m_swap_total_gb << " GB)";
    } else {
        swap_ss << "(None)";
    }
    draw_metric_row(72, swap_label, m_swap_percent, swap_ss.str(), config.colors.accent_purple);

    // Row 4: GPU
    draw_metric_row(102, "󰢮 GPU", m_gpu_percent, m_gpu_name, config.colors.warning);

    g_object_unref(layout);

    // Click anywhere to collapse
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
