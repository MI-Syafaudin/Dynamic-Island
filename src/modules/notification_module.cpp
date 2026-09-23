#include "notification_module.hpp"

namespace di {

NotificationModule::NotificationModule() {}

void NotificationModule::update() {}

void NotificationModule::post_notification(const std::string& app_name, const std::string& message) {
    m_app_name = app_name.empty() ? "Notification" : app_name;
    m_message = message;
}

void NotificationModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    if (m_message.empty()) return;

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, "󰂚 1", -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 1.0);
    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "notification"});
    current_x += tw + 10;

    g_object_unref(layout);
}

void NotificationModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // App Name Header
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string header = "󰂚 " + m_app_name;
    pango_layout_set_text(layout, header.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Message Body
    pango_layout_set_font_description(layout, font_desc);
    std::string msg = m_message;
    if (msg.length() > 42) msg = msg.substr(0, 40) + "...";
    pango_layout_set_text(layout, msg.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.95);
    cairo_move_to(cr, 20, 32);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
