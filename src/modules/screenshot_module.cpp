#include "screenshot_module.hpp"
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#include <cstring>
#include <algorithm>

namespace di {

ScreenshotModule::ScreenshotModule() {}

void ScreenshotModule::update() {}

std::string ScreenshotModule::get_display_path() const {
    if (m_last_path.empty()) return "Copied to clipboard";
    std::string disp_path = m_last_path;
    const char* home = std::getenv("HOME");
    if (home && disp_path.rfind(home, 0) == 0) {
        disp_path = "~" + disp_path.substr(std::strlen(home));
    }
    return disp_path;
}

void ScreenshotModule::take_screenshot(bool area_select) {
    const char* home = std::getenv("HOME");
    std::string dir = (home ? std::string(home) + "/Pictures/Screenshots" : ".");
    std::filesystem::create_directories(dir);

    std::time_t t = std::time(nullptr);
    std::tm* tm_now = std::localtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", tm_now);

    std::string file_path = dir + "/screenshot_" + buf + ".png";

    std::stringstream cmd;
    if (area_select) {
        cmd = std::stringstream();
        cmd << "grim -g \"$(slurp)\" \"" << file_path << "\" 2>/dev/null && wl-copy < \"" << file_path << "\" 2>/dev/null &";
    } else {
        cmd = std::stringstream();
        cmd << "grim \"" << file_path << "\" 2>/dev/null && wl-copy < \"" << file_path << "\" 2>/dev/null &";
    }

    std::system(cmd.str().c_str());
    m_last_path = file_path;
}

void ScreenshotModule::set_captured(const std::string& path) {
    m_last_path = path;
}

void ScreenshotModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    (void)cr;
    (void)font_desc;
    (void)config;
    (void)current_x;
    (void)y;
    (void)h;
    (void)hitboxes;
    // Optionally a small camera icon
}

void ScreenshotModule::get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h) {
    std::string disp_path = get_display_path();
    std::string title = "󰹑 Screenshot Captured";

    bool free_desc = false;
    if (!font_desc) {
        std::string font_spec = config.font_family + " " + std::to_string(config.font_size);
        font_desc = pango_font_description_from_string(font_spec.c_str());
        free_desc = true;
    }

    cairo_surface_t* surface = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* cr = cairo_create(surface);
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Title measurement (Bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int tw_title = 0, th_title = 0;
    pango_layout_get_pixel_size(layout, &tw_title, &th_title);
    pango_font_description_free(bold_desc);

    // Path measurement (Regular)
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, disp_path.c_str(), -1);
    pango_layout_set_width(layout, -1);
    int tw_sub = 0, th_sub = 0;
    pango_layout_get_pixel_size(layout, &tw_sub, &th_sub);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (free_desc && font_desc) {
        pango_font_description_free(font_desc);
    }

    int max_text_w = std::max(tw_title, tw_sub);
    int max_allowed_w = (config.max_idle_width > 0) ? std::min(600, config.max_idle_width) : 600;
    w = std::clamp(static_cast<double>(max_text_w + 56), 340.0, static_cast<double>(max_allowed_w));
    h = static_cast<double>(std::max(th_title + th_sub + 28, 64));
}

void ScreenshotModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Title measurement & layout (Centered, Bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string title = "󰹑 Screenshot Captured";
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    int tw_title = 0, th_title = 0;
    pango_layout_get_pixel_size(layout, &tw_title, &th_title);

    // Subtext measurement & layout (Centered, Middle Ellipsize)
    pango_layout_set_font_description(layout, font_desc);
    std::string disp_path = get_display_path();
    pango_layout_set_text(layout, disp_path.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

    int tw_sub = 0, th_sub = 0;
    pango_layout_get_pixel_size(layout, &tw_sub, &th_sub);

    // Vertically center both lines inside island height h
    int total_content_h = th_title + 4 + th_sub;
    int start_y = std::max(8, (h - total_content_h) / 2);

    // Render Title
    pango_layout_set_font_description(layout, bold_desc);
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    cairo_move_to(cr, 24, start_y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Render Subtext
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, disp_path.c_str(), -1);
    pango_layout_set_width(layout, (w - 48) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.95);
    cairo_move_to(cr, 24, start_y + th_title + 4);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
