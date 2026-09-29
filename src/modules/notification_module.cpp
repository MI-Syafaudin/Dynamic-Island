#include "notification_module.hpp"
#include <algorithm>

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

void NotificationModule::get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h) {
    if (m_message.empty() && m_app_name.empty()) {
        w = 340.0;
        h = 64.0;
        return;
    }

    bool free_desc = false;
    if (!font_desc) {
        std::string font_spec = config.font_family + " " + std::to_string(config.font_size);
        font_desc = pango_font_description_from_string(font_spec.c_str());
        free_desc = true;
    }

    cairo_surface_t* surface = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* cr = cairo_create(surface);
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // 1. Header size (Bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);
    std::string header = "󰂚 " + (m_app_name.empty() ? "Notification" : m_app_name);
    pango_layout_set_text(layout, header.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int hw = 0, hh = 0;
    pango_layout_get_pixel_size(layout, &hw, &hh);
    pango_font_description_free(bold_desc);

    // 2. Measure unwrapped single-line message
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, m_message.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int raw_mw = 0, raw_mh = 0;
    pango_layout_get_pixel_size(layout, &raw_mw, &raw_mh);

    // Determine target width
    int max_allowed_w = (config.max_idle_width > 0) ? std::min(580, config.max_idle_width) : 580;
    int desired_content_w = std::max(hw, raw_mw);
    int chosen_w = std::clamp(desired_content_w + 56, 340, max_allowed_w);

    // Test message wrapping and height with chosen width
    int text_box_w = chosen_w - 56;
    pango_layout_set_width(layout, text_box_w * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, -3); // Maximum 3 lines

    int final_mw = 0, final_mh = 0;
    pango_layout_get_pixel_size(layout, &final_mw, &final_mh);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (free_desc && font_desc) {
        pango_font_description_free(font_desc);
    }

    w = static_cast<double>(chosen_w);
    h = static_cast<double>(std::max(hh + final_mh + 28, 64));
}

void NotificationModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // App Name Header (Bold, Centered)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string header = "󰂚 " + (m_app_name.empty() ? "Notification" : m_app_name);
    pango_layout_set_text(layout, header.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, 0);

    int hw = 0, hh = 0;
    pango_layout_get_pixel_size(layout, &hw, &hh);

    // Message Body measurement
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, m_message.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, -3); // Max 3 lines

    int mw = 0, mh = 0;
    pango_layout_get_pixel_size(layout, &mw, &mh);

    // Vertically center both elements inside island height h
    int total_content_h = hh + 4 + mh;
    int start_y = std::max(8, (h - total_content_h) / 2);

    // Render Header
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, header.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, 0);

    cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 1.0);
    cairo_move_to(cr, 24, start_y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Render Message Body
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, m_message.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(layout, -3);

    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.95);
    cairo_move_to(cr, 24, start_y + hh + 4);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
