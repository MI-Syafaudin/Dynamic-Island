#include "window_module.hpp"
#include <cstdio>
#include <cstdlib>

namespace di {

WindowModule::WindowModule() {
    refresh_active_window();
}

void WindowModule::refresh_active_window() {
    FILE* fp = popen("hyprctl activewindow -j 2>/dev/null", "r");
    if (!fp) return;

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        result += buffer;
    }
    pclose(fp);

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
}

void WindowModule::set_active_window(const std::string& app_class, const std::string& app_title) {
    m_class = app_class;
    m_title = app_title;
}

void WindowModule::update() {
    // Driven by socket2 activewindow>> event
}

void WindowModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    std::string display_name = m_class.empty() ? m_title : m_class;
    if (display_name.empty()) return;

    if (display_name.length() > 14) {
        display_name = display_name.substr(0, 12) + "..";
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
}

} // namespace di
