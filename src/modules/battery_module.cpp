#include "battery_module.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace di {

BatteryModule::BatteryModule() {
    detect_battery_path();
    read_battery_sysfs();
    m_was_charging = m_charging;
    m_first_charging_check = false;
}

void BatteryModule::detect_battery_path() {
    std::string base = "/sys/class/power_supply";
    if (std::filesystem::exists(base)) {
        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            std::string name = entry.path().filename().string();
            if (name.find("BAT") != std::string::npos) {
                m_bat_path = entry.path().string();
                m_available = true;
                return;
            }
        }
    }
    m_available = false;
}

void BatteryModule::read_battery_sysfs() {
    if (!m_available || m_bat_path.empty()) return;

    FILE* cap_file = fopen((m_bat_path + "/capacity").c_str(), "r");
    if (cap_file) {
        int cap = 0;
        if (fscanf(cap_file, "%d", &cap) == 1) {
            m_percentage = cap;
        }
        fclose(cap_file);
    }

    FILE* status_file = fopen((m_bat_path + "/status").c_str(), "r");
    if (status_file) {
        char st[32];
        if (fscanf(status_file, "%31s", st) == 1) {
            m_charging = (strcmp(st, "Charging") == 0);
        }
        fclose(status_file);
    }

    // Auto-clear alarm and reset snooze when charging or when battery level is above critical
    if (m_charging) {
        m_alarm_active = false;
        m_is_snoozed = false;
        m_last_notified_tier = 0;
        m_alert_level = 0;
    } else if (m_percentage > 20 && !m_test_mode) {
        m_alarm_active = false;
        m_is_snoozed = false;
        m_alert_level = 0;
    }
}

void BatteryModule::update() {
    read_battery_sysfs();
}

void BatteryModule::dismiss_alarm() {
    m_alarm_active = false;
    m_alert_level = 0;
}

void BatteryModule::snooze_alarm(int minutes) {
    m_alarm_active = false;
    m_alert_level = 0;
    m_is_snoozed = true;
    m_snooze_until = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
}

bool BatteryModule::is_snoozed() const {
    if (!m_is_snoozed) return false;
    return std::chrono::steady_clock::now() < m_snooze_until;
}

void BatteryModule::trigger_test_alert(int level) {
    m_test_mode = true;
    m_test_level = level;
    m_alert_level = level;
    m_alarm_active = (level <= 10);
    m_is_snoozed = false;
}

BatteryStatusInfo BatteryModule::get_status_info(const Config& config, int display_pct, bool charging) const {
    BatteryStatusInfo info;
    if (charging) {
        if (display_pct >= 95) {
            info.title = "󰂄 Baterai Terisi Penuh";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Terhubung ke pengisi daya";
        } else {
            info.title = "󰂄 Pengisian Daya Aktif";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Baterai sedang diisi";
        }
        info.color = config.colors.accent;
    } else {
        if (display_pct <= 5) {
            info.title = "󰂃 Baterai Sangat Kritis!";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Hubungkan pengisi daya sekarang!";
            info.color = config.colors.danger;
        } else if (display_pct <= 10) {
            info.title = "󰂃 Baterai Kritis!";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Segera hubungkan pengisi daya";
            info.color = config.colors.danger;
        } else if (display_pct <= 20) {
            info.title = "󰁺 Baterai Lemah!";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Hubungkan pengisi daya";
            info.color = config.colors.warning;
        } else if (display_pct < 50) {
            info.title = "󰁼 Baterai Cukup";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Menggunakan daya baterai";
            info.color = config.colors.text;
        } else if (display_pct < 90) {
            info.title = "󰂁 Baterai Baik";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Menggunakan daya baterai";
            info.color = config.colors.text;
        } else {
            info.title = "󰁹 Baterai Optimal";
            info.subtitle = "Tersisa " + std::to_string(display_pct) + "% • Daya baterai optimal";
            info.color = config.colors.accent;
        }
    }
    return info;
}

void BatteryModule::get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr) {
    bool free_desc = false;
    if (!font_desc) {
        std::string font_spec = config.font_family + " " + std::to_string(config.font_size);
        font_desc = pango_font_description_from_string(font_spec.c_str());
        free_desc = true;
    }

    bool destroy_cr = false;
    cairo_surface_t* temp_surface = nullptr;
    if (!cr) {
        temp_surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cr = cairo_create(temp_surface);
        destroy_cr = true;
    }
    PangoLayout* layout = pango_cairo_create_layout(cr);

    int display_pct = (m_alert_level > 0 && (m_alarm_active || m_test_mode)) ? m_alert_level : m_percentage;
    BatteryStatusInfo info = get_status_info(config, display_pct, m_charging);

    // 1. Measure Title (Bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, info.title.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int hw = 0, hh = 0;
    pango_layout_get_pixel_size(layout, &hw, &hh);
    pango_font_description_free(bold_desc);

    // 2. Measure Subtitle
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, info.subtitle.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int sw = 0, sh = 0;
    pango_layout_get_pixel_size(layout, &sw, &sh);

    g_object_unref(layout);
    if (destroy_cr) {
        cairo_destroy(cr);
        cairo_surface_destroy(temp_surface);
    }

    if (free_desc && font_desc) {
        pango_font_description_free(font_desc);
    }

    int max_allowed_w = (config.max_idle_width > 0) ? std::min(600, config.max_idle_width) : 600;
    int desired_w = std::max(hw, sw) + 72; // Safe 36px margin on left and right
    w = static_cast<double>(std::clamp(desired_w, 360, max_allowed_w));
    h = static_cast<double>(std::max(hh + sh + 24, 62));
}

void BatteryModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    if (!m_available) return;

    std::string icon;
    if (m_charging) {
        icon = "󰂄";
    } else {
        if (m_percentage >= 90) icon = "󰁹";
        else if (m_percentage >= 70) icon = "󰂁";
        else if (m_percentage >= 50) icon = "󰁾";
        else if (m_percentage >= 21) icon = "󰁼";
        else if (m_percentage >= 11) icon = "󰁺";
        else icon = "󰂃";
    }

    std::string text = (m_alarm_active ? "󰂃 " : (icon + " ")) + std::to_string(m_percentage) + "%";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_charging) {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    } else if (m_alarm_active || m_percentage <= 10) {
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
    } else if (m_percentage <= 20) {
        cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.9);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x, y, tw, h, "battery_info", ""});
    current_x += tw + 10;

    g_object_unref(layout);
}

void BatteryModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    int display_pct = (m_alert_level > 0 && (m_alarm_active || m_test_mode)) ? m_alert_level : m_percentage;
    BatteryStatusInfo info = get_status_info(config, display_pct, m_charging);

    // 1. Title Layout (Bold, Centered)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, info.title.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    int hw = 0, hh = 0;
    pango_layout_get_pixel_size(layout, &hw, &hh);

    // 2. Subtitle Layout (Centered, Wrapped)
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, info.subtitle.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    int sw = 0, sh = 0;
    pango_layout_get_pixel_size(layout, &sw, &sh);

    // Vertically center inside island height h
    int total_content_h = hh + 4 + sh;
    int start_y = std::max(6, (h - total_content_h) / 2);

    // Draw Title
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, info.title.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

    cairo_set_source_rgba(cr, info.color.r, info.color.g, info.color.b, 1.0);
    cairo_move_to(cr, 24, start_y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Draw Subtitle
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, info.subtitle.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.95);
    cairo_move_to(cr, 24, start_y + hh + 4);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    // Clicking anywhere on the island collapses / dismisses
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

bool BatteryModule::handle_click(const std::string& action, const std::string& param) {
    (void)param;
    if (action == "battery_alarm_dismiss" || action == "battery_dismiss") {
        dismiss_alarm();
        return true;
    }
    if (action == "battery_alarm_snooze" || action == "battery_snooze") {
        snooze_alarm(5);
        return true;
    }
    return false;
}

bool BatteryModule::check_critical_alert(int& out_level, std::string& out_msg) {
    if (m_test_mode) {
        m_test_mode = false;
        m_alarm_active = (m_test_level <= 10);
        m_last_notified_tier = (m_test_level <= 5) ? 3 : ((m_test_level <= 10) ? 2 : 1);
        m_last_notif_time = std::chrono::steady_clock::now();
        out_level = m_test_level;
        m_alert_level = m_test_level;
        return false;
    }

    if (!m_available) return false;

    // Reset notification tracking when plugged into AC / charging
    if (m_charging) {
        m_last_notified_tier = 0;
        m_alarm_active = false;
        m_is_snoozed = false;
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    // Check if snooze is currently active
    if (m_is_snoozed) {
        if (now < m_snooze_until) {
            // Only emergency (<=5%) can override snooze
            if (m_percentage > 5 || m_last_notified_tier >= 3) {
                return false;
            }
        } else {
            m_is_snoozed = false;
        }
    }

    // Determine current severity tier
    int current_tier = 0;
    if (m_percentage <= 5) {
        current_tier = 3; // Emergency / Sangat Kritis
    } else if (m_percentage <= 10) {
        current_tier = 2; // Critical / Kritis
    } else if (m_percentage <= 20) {
        current_tier = 1; // Low / Lemah
    }

    if (current_tier == 0) {
        m_last_notified_tier = 0;
        m_alarm_active = false;
        return false;
    }

    bool should_notify = false;

    if (current_tier > m_last_notified_tier) {
        // Crossed down into a more severe tier
        should_notify = true;
    } else if (current_tier >= 2 && !m_is_snoozed && !m_alarm_active) {
        // Remind every 5 minutes if still critical (<=10%) and discharging
        auto elapsed_min = std::chrono::duration_cast<std::chrono::minutes>(now - m_last_notif_time).count();
        if (elapsed_min >= 5) {
            should_notify = true;
        }
    }

    if (should_notify) {
        m_last_notified_tier = current_tier;
        m_last_notif_time = now;
        out_level = m_percentage;
        m_alert_level = m_percentage;

        if (current_tier == 3) {
            out_msg = "󰂃 Baterai Sangat Kritis (" + std::to_string(m_percentage) + "%)! Hubungkan pengisi daya sekarang!";
        } else if (current_tier == 2) {
            out_msg = "󰂃 Baterai Kritis (" + std::to_string(m_percentage) + "%)! Segera hubungkan pengisi daya.";
        } else {
            out_msg = "󰁺 Baterai Lemah (" + std::to_string(m_percentage) + "%). Hubungkan pengisi daya.";
        }

        if (current_tier >= 2) {
            m_alarm_active = true;
        }
        return true;
    }

    return false;
}

bool BatteryModule::check_charging_started() {
    if (!m_available) return false;
    if (m_first_charging_check) {
        m_was_charging = m_charging;
        m_first_charging_check = false;
        return false;
    }
    if (!m_was_charging && m_charging) {
        m_was_charging = true;
        return true;
    }
    m_was_charging = m_charging;
    return false;
}

} // namespace di
