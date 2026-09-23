#include "screenshot_module.hpp"
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace di {

ScreenshotModule::ScreenshotModule() {}

void ScreenshotModule::update() {}

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
    // Optionally a small camera icon
}

void ScreenshotModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Title: 󰹑 Screenshot Captured
    pango_layout_set_text(layout, "󰹑 Screenshot Captured", -1);
    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // Subtext: Path / copied to clipboard
    std::string disp_path = m_last_path.empty() ? "Copied to clipboard" : m_last_path;
    if (disp_path.length() > 36) disp_path = ".." + disp_path.substr(disp_path.length() - 34);
    pango_layout_set_text(layout, disp_path.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, 20, 28);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
