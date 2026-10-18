#include "window_module.hpp"
#include "../hyprland_ipc.hpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace di {

WindowModule::WindowModule(const Config* config) : m_config(config) {
    refresh_active_window();
}

static std::string to_lower_str(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return str;
}

std::string WindowModule::get_app_icon(const std::string& app_class, const std::string& app_title, const Config* config) {
    std::string s = to_lower_str(app_class);
    if (s.empty()) s = to_lower_str(app_title);
    if (s.empty()) return "󰘔";

    if (config && !config->window_icons.empty()) {
        for (const auto& item : config->window_icons) {
            if (s.find(item.first) != std::string::npos) {
                return item.second;
            }
        }
    }

    // Built-in fallback keywords
    if (s.find("terminal") != std::string::npos || s.find("kitty") != std::string::npos ||
        s.find("alacritty") != std::string::npos || s.find("foot") != std::string::npos) return "";
    if (s.find("firefox") != std::string::npos || s.find("browser") != std::string::npos ||
        s.find("chrome") != std::string::npos) return "󰈹";
    if (s.find("code") != std::string::npos || s.find("nvim") != std::string::npos ||
        s.find("vim") != std::string::npos) return "󰨞";
    if (s.find("thunar") != std::string::npos || s.find("nautilus") != std::string::npos ||
        s.find("dolphin") != std::string::npos || s.find("files") != std::string::npos) return "󰉋";
    if (s.find("spotify") != std::string::npos || s.find("music") != std::string::npos) return "󰓇";
    if (s.find("discord") != std::string::npos || s.find("telegram") != std::string::npos) return "󰙯";

    return "󰘔";
}

void WindowModule::refresh_active_window() {
    m_class.clear();
    m_title.clear();

    std::string result = HyprlandIpc::request("j/activewindow");
    if (result.empty()) {
        m_cached_icon = get_app_icon(m_class, m_title, m_config);
        return;
    }

    size_t class_pos = result.find("\"class\":");
    if (class_pos != std::string::npos) {
        size_t start = result.find('"', class_pos + 8);
        size_t end = result.find('"', start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            m_class = result.substr(start + 1, end - start - 1);
        }
    }

    size_t title_pos = result.find("\"title\":");
    if (title_pos != std::string::npos) {
        size_t start = result.find('"', title_pos + 8);
        size_t end = result.find('"', start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            m_title = result.substr(start + 1, end - start - 1);
        }
    }

    m_cached_icon = get_app_icon(m_class, m_title, m_config);
}

void WindowModule::set_active_window(const std::string& app_class, const std::string& app_title) {
    m_class = app_class;
    m_title = app_title;
    // Cache the icon once upon window switch - 0 cost in render loop
    m_cached_icon = get_app_icon(m_class, m_title, m_config);
}

void WindowModule::update() {
    // Driven by socket2 activewindow>> event
}

void WindowModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    if (m_class.empty() && m_title.empty()) return;

    if (config.window_display_mode == "text") {
        std::string display_name = m_class.empty() ? m_title : m_class;
        if (display_name.empty()) return;

        if (config.max_window_title_length > 0 && static_cast<int>(display_name.length()) > config.max_window_title_length) {
            int keep = std::max(1, config.max_window_title_length - 2);
            display_name = display_name.substr(0, keep) + "..";
        }

        PangoLayout* layout = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(layout, font_desc);
        pango_layout_set_text(layout, display_name.c_str(), -1);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);

        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.85);
        cairo_move_to(cr, current_x, y + (h - th) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({current_x, y, tw, h, "window_info", m_title});
        current_x += tw + 10;
        g_object_unref(layout);
        return;
    }

    // Direct read from m_cached_icon without any search / reallocation
    const std::string& icon = m_cached_icon.empty() ? "󰘔" : m_cached_icon;
    std::string display_text = icon;
    if (config.window_display_mode == "both") {
        std::string name = m_class.empty() ? m_title : m_class;
        if (config.max_window_title_length > 0 && static_cast<int>(name.length()) > config.max_window_title_length) {
            int keep = std::max(1, config.max_window_title_length - 2);
            name = name.substr(0, keep) + "..";
        }
        display_text += " " + name;
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, display_text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    int item_w = std::max(tw + 12, 22);
    int item_h = 20;
    int item_y = y + (h - item_h) / 2;

    // Draw stylish pill badge background for active app logo
    cairo_new_sub_path(cr);
    double r = item_h / 2.0;
    cairo_arc(cr, current_x + r, item_y + r, r, M_PI / 2, 3 * M_PI / 2);
    cairo_arc(cr, current_x + item_w - r, item_y + r, r, -M_PI / 2, M_PI / 2);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.85);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Active logo color in bright accent
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
    cairo_move_to(cr, current_x + (item_w - tw) / 2, item_y + (item_h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x, item_y, item_w, item_h, "window_info", m_title});
    current_x += item_w + 6;

    g_object_unref(layout);
}

} // namespace di
