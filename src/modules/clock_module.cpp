#include "clock_module.hpp"
#include <sys/sysinfo.h>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include "../json.hpp"

namespace di {

ClockModule::ClockModule() {
    load_alarms();
    std::time_t t = std::time(nullptr);
    std::tm* tm_now = std::localtime(&t);
    if (tm_now) {
        m_set_hour = (tm_now->tm_hour + 1) % 24;
        m_set_minute = 0;
    }
    update(true);
}

std::string ClockModule::get_alarms_file_path() const {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::string(xdg_config) + "/dynamic-island/alarms.json";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/dynamic-island/alarms.json";
    }
    return "alarms.json";
}

void ClockModule::load_alarms() {
    std::string path = get_alarms_file_path();
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    if (content.empty()) return;

    JsonValue root = JsonParser::parse(content);
    if (!root.is_array()) return;

    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);

    m_alarms.clear();
    int max_id = 0;

    for (const auto& item : root.arr_val) {
        if (!item.is_object()) continue;
        AlarmItem alarm;
        alarm.id = item.has("id") ? item["id"].as_int() : 1;
        alarm.hour = item.has("hour") ? item["hour"].as_int() : -1;
        alarm.minute = item.has("minute") ? item["minute"].as_int() : -1;
        alarm.label = item.has("label") ? item["label"].as_string() : "Alarm";
        alarm.is_timer = item.has("is_timer") ? item["is_timer"].as_bool() : false;
        alarm.total_seconds = item.has("total_seconds") ? item["total_seconds"].as_int() : 0;
        alarm.is_ringing = false;

        time_t target_epoch = item.has("target_epoch") ? static_cast<time_t>(item["target_epoch"].as_int()) : 0;

        if (alarm.is_timer) {
            if (target_epoch <= now_c) {
                // Expired timer, skip
                continue;
            }
            alarm.target_time = std::chrono::system_clock::from_time_t(target_epoch);
        } else {
            // Wall-clock alarm
            if (alarm.hour >= 0 && alarm.minute >= 0) {
                std::tm tm_target = *std::localtime(&now_c);
                tm_target.tm_hour = alarm.hour;
                tm_target.tm_min = alarm.minute;
                tm_target.tm_sec = 0;
                time_t target_c = std::mktime(&tm_target);
                if (target_c <= now_c) {
                    target_c += 86400; // Next day
                }
                alarm.target_time = std::chrono::system_clock::from_time_t(target_c);
            } else if (target_epoch > now_c) {
                alarm.target_time = std::chrono::system_clock::from_time_t(target_epoch);
            } else {
                continue;
            }
        }

        if (alarm.id > max_id) max_id = alarm.id;
        m_alarms.push_back(alarm);
    }

    m_next_alarm_id = max_id + 1;
}

void ClockModule::save_alarms() const {
    std::string path = get_alarms_file_path();
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    std::ofstream f(path);
    if (!f.is_open()) return;

    f << "[\n";
    bool first = true;
    for (const auto& a : m_alarms) {
        if (!first) f << ",\n";
        first = false;
        time_t epoch = std::chrono::system_clock::to_time_t(a.target_time);
        f << "  {\n"
          << "    \"id\": " << a.id << ",\n"
          << "    \"hour\": " << a.hour << ",\n"
          << "    \"minute\": " << a.minute << ",\n"
          << "    \"label\": \"" << a.label << "\",\n"
          << "    \"is_timer\": " << (a.is_timer ? "true" : "false") << ",\n"
          << "    \"total_seconds\": " << a.total_seconds << ",\n"
          << "    \"target_epoch\": " << epoch << "\n"
          << "  }";
    }
    f << "\n]\n";
}

int ClockModule::add_alarm(int hour, int minute, const std::string& label) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return -1;

    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_target = *std::localtime(&now_c);
    tm_target.tm_hour = hour;
    tm_target.tm_min = minute;
    tm_target.tm_sec = 0;
    time_t target_c = std::mktime(&tm_target);
    if (target_c <= now_c) {
        target_c += 86400; // Tomorrow
    }

    AlarmItem item;
    item.id = m_next_alarm_id++;
    item.hour = hour;
    item.minute = minute;
    item.label = label.empty() ? "Alarm" : label;
    item.target_time = std::chrono::system_clock::from_time_t(target_c);
    item.is_timer = false;
    item.total_seconds = 0;
    item.is_ringing = false;

    m_alarms.push_back(item);
    save_alarms();
    return item.id;
}

int ClockModule::add_timer(int seconds, const std::string& label) {
    if (seconds <= 0) return -1;

    AlarmItem item;
    item.id = m_next_alarm_id++;
    item.hour = -1;
    item.minute = -1;
    if (label.empty()) {
        int m = seconds / 60;
        int s = seconds % 60;
        if (m > 0 && s == 0) item.label = "Timer " + std::to_string(m) + "m";
        else if (m > 0) item.label = "Timer " + std::to_string(m) + "m " + std::to_string(s) + "s";
        else item.label = "Timer " + std::to_string(s) + "s";
    } else {
        item.label = label;
    }
    item.target_time = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    item.is_timer = true;
    item.total_seconds = seconds;
    item.is_ringing = false;

    m_alarms.push_back(item);
    save_alarms();
    return item.id;
}

bool ClockModule::cancel_alarm(int id) {
    if (id == -1) {
        m_alarms.clear();
        save_alarms();
        return true;
    }

    auto it = std::remove_if(m_alarms.begin(), m_alarms.end(), [id](const AlarmItem& a) {
        return a.id == id;
    });
    if (it != m_alarms.end()) {
        m_alarms.erase(it, m_alarms.end());
        save_alarms();
        return true;
    }
    return false;
}

void ClockModule::dismiss_alarm() {
    auto it = std::remove_if(m_alarms.begin(), m_alarms.end(), [](const AlarmItem& a) {
        return a.is_ringing;
    });
    if (it != m_alarms.end()) {
        m_alarms.erase(it, m_alarms.end());
        save_alarms();
    }
}

void ClockModule::snooze_alarm(int minutes) {
    if (minutes <= 0) minutes = 5;
    for (auto& a : m_alarms) {
        if (a.is_ringing) {
            a.target_time = std::chrono::system_clock::now() + std::chrono::minutes(minutes);
            a.is_ringing = false;
        }
    }
    save_alarms();
}

bool ClockModule::is_any_alarm_ringing() const {
    for (const auto& a : m_alarms) {
        if (a.is_ringing) return true;
    }
    return false;
}

const AlarmItem* ClockModule::get_ringing_alarm() const {
    for (const auto& a : m_alarms) {
        if (a.is_ringing) return &a;
    }
    return nullptr;
}

const AlarmItem* ClockModule::get_next_alarm() const {
    const AlarmItem* earliest = nullptr;
    for (const auto& a : m_alarms) {
        if (a.is_ringing) continue;
        if (!earliest || a.target_time < earliest->target_time) {
            earliest = &a;
        }
    }
    return earliest;
}

bool ClockModule::has_active_timer() const {
    auto now = std::chrono::system_clock::now();
    for (const auto& a : m_alarms) {
        if (a.is_timer && !a.is_ringing && a.target_time > now) return true;
    }
    return false;
}

int ClockModule::get_remaining_timer_seconds() const {
    auto now = std::chrono::system_clock::now();
    int min_sec = -1;
    for (const auto& a : m_alarms) {
        if (a.is_timer && !a.is_ringing && a.target_time > now) {
            int sec = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(a.target_time - now).count());
            if (min_sec == -1 || sec < min_sec) {
                min_sec = sec;
            }
        }
    }
    return min_sec >= 0 ? min_sec : 0;
}

bool ClockModule::check_alarms(AlarmItem& out_triggered) {
    auto now = std::chrono::system_clock::now();
    for (auto& a : m_alarms) {
        if (!a.is_ringing && now >= a.target_time) {
            a.is_ringing = true;
            a.ring_start_time = std::chrono::steady_clock::now();
            out_triggered = a;
            return true;
        }
    }
    return false;
}

std::string ClockModule::get_alarms_list_str() const {
    if (m_alarms.empty()) {
        return "No active alarms or timers.\n";
    }

    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "Active Alarms & Timers (" << m_alarms.size() << "):\n";
    for (const auto& a : m_alarms) {
        ss << "  [ID " << a.id << "] ";
        if (a.is_ringing) {
            ss << "🔔 RINGING NOW - \"" << a.label << "\"\n";
            continue;
        }

        if (a.is_timer) {
            int rem = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(a.target_time - now).count());
            if (rem < 0) rem = 0;
            int m = rem / 60;
            int s = rem % 60;
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", m, s);
            ss << "󰔟 Timer (" << tbuf << " left) - \"" << a.label << "\"\n";
        } else {
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", a.hour, a.minute);
            time_t target_c = std::chrono::system_clock::to_time_t(a.target_time);
            long diff_sec = target_c - now_c;
            if (diff_sec < 0) diff_sec = 0;
            long h = diff_sec / 3600;
            long m = (diff_sec % 3600) / 60;
            ss << "󰂞 Alarm " << time_buf << " (in " << h << "h " << m << "m) - \"" << a.label << "\"\n";
        }
    }
    return ss.str();
}

void ClockModule::update_uptime() {
    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        long uptime = s_info.uptime;
        int hours = uptime / 3600;
        int minutes = (uptime % 3600) / 60;
        char buf[64];
        snprintf(buf, sizeof(buf), "Uptime: %dh %dm", hours, minutes);
        m_uptime_str = buf;
    } else {
        m_uptime_str = "Uptime: --";
    }
}

void ClockModule::update(bool expanded) {
    std::time_t t = std::time(nullptr);
    std::tm* tm_now = std::localtime(&t);
    if (!tm_now) return;

    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M", tm_now);
    m_time_str = buf;

    if (expanded) {
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm_now);
        m_full_time_str = buf;

        std::strftime(buf, sizeof(buf), "%A, %d %B %Y", tm_now);
        m_date_str = buf;

        update_uptime();
    } else if (tm_now->tm_min != m_last_minute) {
        m_last_minute = tm_now->tm_min;
        std::strftime(buf, sizeof(buf), "%H:%M:%S", tm_now);
        m_full_time_str = buf;

        std::strftime(buf, sizeof(buf), "%A, %d %B %Y", tm_now);
        m_date_str = buf;
    }
}

void ClockModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    std::string text = m_time_str;
    bool is_ringing = is_any_alarm_ringing();
    bool has_timer = has_active_timer();

    if (is_ringing) {
        text = "󰂚 ALARM!";
    } else if (has_timer) {
        int rem = get_remaining_timer_seconds();
        int m = rem / 60;
        int s = rem % 60;
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", m, s);
        text = m_time_str + " 󰔟 " + tbuf;
    } else if (get_next_alarm() != nullptr) {
        text = m_time_str + " 󰂞";
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int text_w, text_h;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    if (is_ringing) {
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
    } else if (has_timer) {
        cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, config.colors.text.a);
    }

    cairo_move_to(cr, current_x, y + (h - text_h) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 4, y, text_w + 8, h, "toggle_expand", "clock"});
    current_x += text_w + 12;

    g_object_unref(layout);
}

void ClockModule::adjust_hour(int delta) {
    m_set_hour = (m_set_hour + delta) % 24;
    if (m_set_hour < 0) m_set_hour += 24;
}

void ClockModule::adjust_minute(int delta) {
    m_set_minute = (m_set_minute + delta) % 60;
    if (m_set_minute < 0) m_set_minute += 60;
}

void ClockModule::set_selected_time(int hour, int minute) {
    m_set_hour = std::clamp(hour, 0, 23);
    m_set_minute = std::clamp(minute, 0, 59);
}

void ClockModule::handle_scroll(double x, double y, double delta) {
    (void)y;
    int dir = (delta > 0) ? -1 : 1;
    if (m_hour_box_w > 0 && x >= m_hour_box_x - 10 && x <= m_hour_box_x + m_hour_box_w + 10) {
        adjust_hour(dir);
    } else if (m_min_box_w > 0 && x >= m_min_box_x - 10 && x <= m_min_box_x + m_min_box_w + 10) {
        adjust_minute(dir);
    } else {
        adjust_minute(dir * 5);
    }
}

void ClockModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // 1. Large Time
    PangoFontDescription* large_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(large_desc, 20 * PANGO_SCALE);
    pango_font_description_set_weight(large_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, large_desc);
    pango_layout_set_text(layout, m_full_time_str.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
    cairo_move_to(cr, (w - tw) / 2, 8);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(large_desc);

    // 2. Full Date (Centered, clearly visible, protected against truncation)
    pango_layout_set_font_description(layout, font_desc);
    std::string date_display = "󰸗 " + m_date_str;
    pango_layout_set_text(layout, date_display.c_str(), -1);
    pango_layout_set_width(layout, (w - 40) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.95);
    cairo_move_to(cr, 20, 34);
    pango_cairo_show_layout(cr, layout);

    // 3. System Uptime (Subtle line below date)
    std::string uptime_display = "󰔟 " + m_uptime_str;
    pango_layout_set_text(layout, uptime_display.c_str(), -1);
    pango_layout_set_width(layout, (w - 40) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.70);
    cairo_move_to(cr, 20, 50);
    pango_cairo_show_layout(cr, layout);

    // Reset layout alignment and width for subsequent custom components
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    // 4. Subtle separator line
    cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.40);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 22, 68);
    cairo_line_to(cr, w - 22, 68);
    cairo_stroke(cr);

    // 5. Alarm / Timer Status
    const AlarmItem* next_a = get_next_alarm();
    std::string alarm_text;
    if (next_a) {
        if (next_a->is_timer) {
            auto now = std::chrono::system_clock::now();
            int rem = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(next_a->target_time - now).count());
            if (rem < 0) rem = 0;
            int m = rem / 60;
            int s = rem % 60;
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", m, s);
            alarm_text = "󰔟 Timer: " + std::string(tbuf) + " (" + next_a->label + ")";
        } else {
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", next_a->hour, next_a->minute);
            alarm_text = "󰂞 Alarm: " + std::string(tbuf) + " (" + next_a->label + ")";
        }
    } else {
        alarm_text = "󰂞 Tidak ada alarm aktif";
    }

    if (next_a) {
        // "Batal" badge button on right
        std::string cancel_txt = "󰅖 Batal";
        pango_layout_set_text(layout, cancel_txt.c_str(), -1);
        int cw, ch;
        pango_layout_get_pixel_size(layout, &cw, &ch);

        int bx = w - 24 - cw - 12;
        int by = 73;
        int bw = cw + 12;
        int bh = 20;

        cairo_new_sub_path(cr);
        double br = 4.0;
        cairo_arc(cr, bx + br, by + br, br, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + bw - br, by + br, br, -M_PI / 2, 0);
        cairo_arc(cr, bx + bw - br, by + bh - br, br, 0, M_PI / 2);
        cairo_arc(cr, bx + br, by + bh - br, br, M_PI / 2, M_PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 0.25);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 0.6);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
        cairo_move_to(cr, bx + 6, by + (bh - ch) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({bx, by, bw, bh, "alarm_cancel", std::to_string(next_a->id)});

        // Active alarm text on left (bounded so it never overlaps the cancel button)
        pango_layout_set_text(layout, alarm_text.c_str(), -1);
        int max_al_w = bx - 24 - 10;
        if (max_al_w > 50) {
            pango_layout_set_width(layout, max_al_w * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        }
        cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 1.0);
        cairo_move_to(cr, 24, 75);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    } else {
        pango_layout_set_text(layout, alarm_text.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.75);
        cairo_move_to(cr, (w - tw) / 2, 75);
        pango_cairo_show_layout(cr, layout);
    }

    // 6. Presets Row (Templates preserved & enhanced)
    const std::vector<std::pair<std::string, std::string>> presets = {
        {"+1m", "1"}, {"+5m", "5"}, {"+10m", "10"}, {"+15m", "15"}, {"+30m", "30"}, {"+1h", "60"}
    };

    int btn_w = 48;
    int btn_h = 22;
    int btn_spacing = 6;
    int total_btns_w = static_cast<int>(presets.size()) * btn_w + (static_cast<int>(presets.size()) - 1) * btn_spacing;
    int start_btn_x = (w - total_btns_w) / 2;
    int btn_y = 98;

    for (size_t i = 0; i < presets.size(); ++i) {
        int cur_bx = start_btn_x + static_cast<int>(i) * (btn_w + btn_spacing);

        cairo_new_sub_path(cr);
        double r = 4.0;
        cairo_arc(cr, cur_bx + r, btn_y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, cur_bx + btn_w - r, btn_y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, cur_bx + btn_w - r, btn_y + btn_h - r, r, 0, M_PI / 2);
        cairo_arc(cr, cur_bx + r, btn_y + btn_h - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.85);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.6);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        pango_layout_set_text(layout, presets[i].first.c_str(), -1);
        int btw, bth;
        pango_layout_get_pixel_size(layout, &btw, &bth);

        cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
        cairo_move_to(cr, cur_bx + (btn_w - btw) / 2, btn_y + (btn_h - bth) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({cur_bx, btn_y, btn_w, btn_h, "quick_timer", presets[i].second});
    }

    // 7. Subtle divider
    cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.4);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 22, 126);
    cairo_line_to(cr, w - 22, 126);
    cairo_stroke(cr);

    // 8. Custom Time Stepper Row (Setting Waktu yang Diinginkan)
    int step_y = 134;
    int step_h = 24;

    std::string step_lbl = "󰥔 Atur:";
    pango_layout_set_text(layout, step_lbl.c_str(), -1);
    int lw, lh;
    pango_layout_get_pixel_size(layout, &lw, &lh);

    int btn_step_w = 24;
    int box_w = 34;
    int gap = 3;

    int total_step_w = lw + 14 + (btn_step_w * 4) + (box_w * 2) + (gap * 4) + 18;
    int cur_x = (w - total_step_w) / 2;

    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.95);
    cairo_move_to(cr, cur_x, step_y + (step_h - lh) / 2);
    pango_cairo_show_layout(cr, layout);
    cur_x += lw + 14;

    auto draw_stepper_btn = [&](int bx, int by, int bw, int bh, const std::string& txt, const std::string& action, const std::string& param) {
        cairo_new_sub_path(cr);
        double r = 4.0;
        cairo_arc(cr, bx + r, by + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + bw - r, by + r, r, -M_PI / 2, 0);
        cairo_arc(cr, bx + bw - r, by + bh - r, r, 0, M_PI / 2);
        cairo_arc(cr, bx + r, by + bh - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.7);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        pango_layout_set_text(layout, txt.c_str(), -1);
        int btw, bth;
        pango_layout_get_pixel_size(layout, &btw, &bth);
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        cairo_move_to(cr, bx + (bw - btw) / 2, by + (bh - bth) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({bx, by, bw, bh, action, param});
    };

    // Hour dec [-]
    draw_stepper_btn(cur_x, step_y, btn_step_w, step_h, "-", "time_adj", "h_dec");
    cur_x += btn_step_w + gap;

    // Hour box [HH]
    char h_str[8];
    snprintf(h_str, sizeof(h_str), "%02d", m_set_hour);
    draw_stepper_btn(cur_x, step_y, box_w, step_h, h_str, "time_adj", "h_inc");
    m_hour_box_x = cur_x;
    m_hour_box_w = box_w;
    cur_x += box_w + gap;

    // Hour inc [+]
    draw_stepper_btn(cur_x, step_y, btn_step_w, step_h, "+", "time_adj", "h_inc");
    cur_x += btn_step_w + 6;

    // Colon separator
    pango_layout_set_text(layout, ":", -1);
    int col_w, col_h;
    pango_layout_get_pixel_size(layout, &col_w, &col_h);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, cur_x, step_y + (step_h - col_h) / 2);
    pango_cairo_show_layout(cr, layout);
    cur_x += col_w + 6;

    // Minute dec [-]
    draw_stepper_btn(cur_x, step_y, btn_step_w, step_h, "-", "time_adj", "m_dec");
    cur_x += btn_step_w + gap;

    // Minute box [MM]
    char m_str[8];
    snprintf(m_str, sizeof(m_str), "%02d", m_set_minute);
    draw_stepper_btn(cur_x, step_y, box_w, step_h, m_str, "time_adj", "m_inc");
    m_min_box_x = cur_x;
    m_min_box_w = box_w;
    cur_x += box_w + gap;

    // Minute inc [+]
    draw_stepper_btn(cur_x, step_y, btn_step_w, step_h, "+", "time_adj", "m_inc");

    m_stepper_y = step_y;
    m_stepper_h = step_h;

    // 9. Action Buttons: [ 󰂞 Alarm HH:MM ]  and  [ 󰔟 Mulai Timer ]
    int act_y = 166;
    int act_h = 26;
    int act_w = 156;
    int act_spacing = 16;
    int act_start_x = (w - (act_w * 2 + act_spacing)) / 2;

    // Button 1: Set Alarm HH:MM
    int b1_x = act_start_x;
    cairo_new_sub_path(cr);
    double br1 = 5.0;
    cairo_arc(cr, b1_x + br1, act_y + br1, br1, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, b1_x + act_w - br1, act_y + br1, br1, -M_PI / 2, 0);
    cairo_arc(cr, b1_x + act_w - br1, act_y + act_h - br1, br1, 0, M_PI / 2);
    cairo_arc(cr, b1_x + br1, act_y + act_h - br1, br1, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.25);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    char al_txt[32];
    snprintf(al_txt, sizeof(al_txt), "󰂞 Alarm %02d:%02d", m_set_hour, m_set_minute);
    pango_layout_set_text(layout, al_txt, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_move_to(cr, b1_x + (act_w - tw) / 2, act_y + (act_h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({b1_x, act_y, act_w, act_h, "set_custom_alarm", ""});

    // Button 2: Set Timer
    int b2_x = b1_x + act_w + act_spacing;
    cairo_new_sub_path(cr);
    cairo_arc(cr, b2_x + br1, act_y + br1, br1, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, b2_x + act_w - br1, act_y + br1, br1, -M_PI / 2, 0);
    cairo_arc(cr, b2_x + act_w - br1, act_y + act_h - br1, br1, 0, M_PI / 2);
    cairo_arc(cr, b2_x + br1, act_y + act_h - br1, br1, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.95);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    char tm_txt[32];
    if (m_set_hour == 0 && m_set_minute > 0) {
        snprintf(tm_txt, sizeof(tm_txt), "󰔟 Timer %dm", m_set_minute);
    } else if (m_set_hour > 0 && m_set_minute == 0) {
        snprintf(tm_txt, sizeof(tm_txt), "󰔟 Timer %dh", m_set_hour);
    } else if (m_set_hour > 0) {
        snprintf(tm_txt, sizeof(tm_txt), "󰔟 Timer %dh %dm", m_set_hour, m_set_minute);
    } else {
        snprintf(tm_txt, sizeof(tm_txt), "󰔟 Mulai Timer");
    }
    pango_layout_set_text(layout, tm_txt, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
    cairo_move_to(cr, b2_x + (act_w - tw) / 2, act_y + (act_h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({b2_x, act_y, act_w, act_h, "set_custom_timer", ""});

    g_object_unref(layout);

    // Clicking elsewhere closes clock
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

void ClockModule::draw_alarm_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    const AlarmItem* ringing = get_ringing_alarm();
    std::string label = ringing ? ringing->label : "Alarm";
    bool is_timer = ringing ? ringing->is_timer : false;

    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Title / Header (Bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(bold_desc, 13 * PANGO_SCALE);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string title = is_timer ? "󰔟 TIMER SELESAI!" : "󰂚 ALARM BERBUNYI!";
    pango_layout_set_text(layout, title.c_str(), -1);
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
    cairo_move_to(cr, (w - tw) / 2, 10);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Label / Subtitle
    pango_layout_set_font_description(layout, font_desc);
    std::string sub = "\"" + label + "\" • " + m_full_time_str;
    pango_layout_set_text(layout, sub.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.95);
    cairo_move_to(cr, (w - tw) / 2, 32);
    pango_cairo_show_layout(cr, layout);

    // Action buttons: [󰅖 Matikan]  [󰒲 Tunda 5m]
    int btn_h = 28;
    int btn1_w = 120;
    int btn2_w = 110;
    int spacing = 14;
    int total_w = btn1_w + spacing + btn2_w;
    int b1_x = (w - total_w) / 2;
    int b2_x = b1_x + btn1_w + spacing;
    int by = 56;

    // Button 1: Dismiss / Matikan (Red Accent)
    cairo_new_sub_path(cr);
    double r = 6.0;
    cairo_arc(cr, b1_x + r, by + r, r, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, b1_x + btn1_w - r, by + r, r, -M_PI / 2, 0);
    cairo_arc(cr, b1_x + btn1_w - r, by + btn_h - r, r, 0, M_PI / 2);
    cairo_arc(cr, b1_x + r, by + btn_h - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 0.25);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    std::string dis_txt = "󰅖 Matikan";
    pango_layout_set_text(layout, dis_txt.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_move_to(cr, b1_x + (btn1_w - tw) / 2, by + (btn_h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({b1_x, by, btn1_w, btn_h, "alarm_dismiss", ""});

    // Button 2: Snooze / Tunda 5m (Blue Accent)
    cairo_new_sub_path(cr);
    cairo_arc(cr, b2_x + r, by + r, r, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, b2_x + btn2_w - r, by + r, r, -M_PI / 2, 0);
    cairo_arc(cr, b2_x + btn2_w - r, by + btn_h - r, r, 0, M_PI / 2);
    cairo_arc(cr, b2_x + r, by + btn_h - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.9);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    std::string snz_txt = "󰒲 Tunda 5m";
    pango_layout_set_text(layout, snz_txt.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
    cairo_move_to(cr, b2_x + (btn2_w - tw) / 2, by + (btn_h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({b2_x, by, btn2_w, btn_h, "alarm_snooze", ""});

    g_object_unref(layout);

    // Clicking anywhere else dismisses
    hitboxes.push_back({0, 0, w, h, "alarm_dismiss", ""});
}

bool ClockModule::handle_click(const std::string& action, const std::string& param) {
    if (action == "quick_timer") {
        int minutes = std::atoi(param.c_str());
        if (minutes > 0) {
            std::string lbl = (minutes >= 60) ? ("Timer " + std::to_string(minutes / 60) + "h") : ("Timer " + param + "m");
            add_timer(minutes * 60, lbl);
            return true;
        }
    } else if (action == "alarm_cancel") {
        int id = std::atoi(param.c_str());
        cancel_alarm(id);
        return true;
    } else if (action == "alarm_dismiss") {
        dismiss_alarm();
        return true;
    } else if (action == "alarm_snooze") {
        snooze_alarm(5);
        return true;
    } else if (action == "time_adj") {
        if (param == "h_inc") adjust_hour(1);
        else if (param == "h_dec") adjust_hour(-1);
        else if (param == "m_inc") adjust_minute(5);
        else if (param == "m_dec") adjust_minute(-5);
        else if (param == "m_inc1") adjust_minute(1);
        else if (param == "m_dec1") adjust_minute(-1);
        return true;
    } else if (action == "set_custom_alarm") {
        char buf[32];
        snprintf(buf, sizeof(buf), "Alarm %02d:%02d", m_set_hour, m_set_minute);
        add_alarm(m_set_hour, m_set_minute, buf);
        return true;
    } else if (action == "set_custom_timer") {
        int total_secs = (m_set_hour * 3600) + (m_set_minute * 60);
        if (total_secs <= 0) total_secs = 300; // default 5m
        add_timer(total_secs, "");
        return true;
    }
    return false;
}

} // namespace di
