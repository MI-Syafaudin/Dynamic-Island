#include "battery_module.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace di {

BatteryModule::BatteryModule() {
    detect_battery_path();
    read_battery_sysfs();
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
}

void BatteryModule::update() {
    read_battery_sysfs();
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
        else if (m_percentage >= 30) icon = "󰁼";
        else if (m_percentage >= 15) icon = "󰁺";
        else icon = "󰂃";
    }

    std::string text = icon + " " + std::to_string(m_percentage) + "%";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_charging) {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    } else if (m_percentage <= 20) {
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.9);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x, y, tw, h, "battery_info", ""});
    current_x += tw + 10;

    g_object_unref(layout);
}

} // namespace di
