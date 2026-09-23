#include "clock_module.hpp"
#include <sys/sysinfo.h>
#include <iomanip>
#include <sstream>

namespace di {

ClockModule::ClockModule() {
    update();
}

void ClockModule::update_uptime() {
    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        long uptime = s_info.uptime;
        int hours = uptime / 3600;
        int minutes = (uptime % 3600) / 60;
        std::stringstream ss;
        ss << "Uptime: " << hours << "h " << minutes << "m";
        m_uptime_str = ss.str();
    } else {
        m_uptime_str = "Uptime: --";
    }
}

void ClockModule::update() {
    std::time_t t = std::time(nullptr);
    std::tm* tm_now = std::localtime(&t);
    if (!tm_now) return;

    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M", tm_now);
    m_time_str = buf;

    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm_now);
    m_full_time_str = buf;

    std::strftime(buf, sizeof(buf), "%A, %d %B %Y", tm_now);
    m_date_str = buf;

    update_uptime();
}

void ClockModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, m_time_str.c_str(), -1);

    int text_w, text_h;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, config.colors.text.a);
    cairo_move_to(cr, current_x, y + (h - text_h) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 4, y, text_w + 8, h, "toggle_expand", "clock"});
    current_x += text_w + 12;

    g_object_unref(layout);
}

void ClockModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Large Time
    PangoFontDescription* large_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(large_desc, 22 * PANGO_SCALE);
    pango_font_description_set_weight(large_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, large_desc);
    pango_layout_set_text(layout, m_full_time_str.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
    cairo_move_to(cr, (w - tw) / 2, 14);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(large_desc);

    // Full Date
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, m_date_str.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    cairo_move_to(cr, (w - tw) / 2, 48);
    pango_cairo_show_layout(cr, layout);

    // Uptime / Subtext
    pango_layout_set_text(layout, m_uptime_str.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, (w - tw) / 2, 70);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    // Clicking anywhere in expanded clock closes it
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

bool ClockModule::handle_click(const std::string& action, const std::string& param) {
    return false;
}

} // namespace di
