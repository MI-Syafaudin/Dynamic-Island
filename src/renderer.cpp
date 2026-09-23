#include "renderer.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace di {

Renderer::Renderer() {}

Renderer::~Renderer() {
    if (m_font_desc) {
        pango_font_description_free(m_font_desc);
        m_font_desc = nullptr;
    }
}

bool Renderer::init(const Config& config) {
    update_font(config);
    return true;
}

void Renderer::update_font(const Config& config) {
    if (m_font_desc) {
        pango_font_description_free(m_font_desc);
    }
    std::string font_spec = config.font_family + " " + std::to_string(config.font_size);
    m_font_desc = pango_font_description_from_string(font_spec.c_str());
}

void Renderer::draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);
}

void Renderer::draw_brightness_expanded(cairo_t* cr, const Config& config, int w, int h, int brightness_val, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, m_font_desc);

    // Title / Icon
    std::string title = "󰃠 Brightness";
    pango_layout_set_text(layout, title.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // Percentage text
    std::string pct_text = std::to_string(brightness_val) + "%";
    pango_layout_set_text(layout, pct_text.c_str(), -1);
    int ptw, pth;
    pango_layout_get_pixel_size(layout, &ptw, &pth);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 1.0);
    cairo_move_to(cr, w - 20 - ptw, 10);
    pango_cairo_show_layout(cr, layout);

    // Slider track
    int bar_x = 20;
    int bar_y = 30;
    int bar_w = w - 40;
    int bar_h = 8;
    double bar_r = 4.0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, bar_x + bar_r, bar_y + bar_r, bar_r, M_PI / 2, 3 * M_PI / 2);
    cairo_arc(cr, bar_x + bar_w - bar_r, bar_y + bar_r, bar_r, -M_PI / 2, M_PI / 2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.22, 0.9);
    cairo_fill(cr);

    // Fill
    double clamped = std::clamp(brightness_val, 0, 100) / 100.0;
    int fill_w = std::max(static_cast<int>(bar_w * clamped), 8);
    if (clamped > 0.0) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, bar_x + bar_r, bar_y + bar_r, bar_r, M_PI / 2, 3 * M_PI / 2);
        cairo_arc(cr, bar_x + fill_w - bar_r, bar_y + bar_r, bar_r, -M_PI / 2, M_PI / 2);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, config.colors.warning.r, config.colors.warning.g, config.colors.warning.b, 0.95);
        cairo_fill(cr);
    }

    g_object_unref(layout);
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

void Renderer::render_island(
    cairo_t* cr,
    int width,
    int height,
    double corner_radius,
    IslandMode mode,
    const Config& config,
    const std::vector<std::shared_ptr<ModuleBase>>& modules,
    std::vector<HitBox>& hitboxes,
    int brightness_val
) {
    hitboxes.clear();

    // 1. Draw subtle shadow around the pill
    cairo_save(cr);
    draw_rounded_rect(cr, 1.0, 1.0, width - 2.0, height - 2.0, corner_radius);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
    cairo_set_line_width(cr, 3.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    // 2. Draw Main Pill Background
    cairo_save(cr);
    draw_rounded_rect(cr, 1.0, 1.0, width - 2.0, height - 2.0, corner_radius);
    cairo_set_source_rgba(cr, config.colors.background.r, config.colors.background.g,
                              config.colors.background.b, config.colors.background.a);
    cairo_fill_preserve(cr);

    // Subtle 1px Border
    cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g,
                              config.colors.border.b, config.colors.border.a);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    // 3. Render contents depending on mode
    if (mode == IslandMode::Idle) {
        int current_x = 12;
        int y = 0;
        int h = height;

        for (const auto& mod : modules) {
            if (mod) {
                mod->draw_compact(cr, m_font_desc, config, current_x, y, h, hitboxes);
            }
        }
    } else {
        // Expanded Modes
        switch (mode) {
            case IslandMode::Expanded_Audio:
                if (modules.size() > 3 && modules[3]) { // AudioModule
                    modules[3]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_Brightness:
                draw_brightness_expanded(cr, config, width, height, brightness_val, hitboxes);
                break;
            case IslandMode::Expanded_Media:
                if (modules.size() > 4 && modules[4]) { // MediaModule
                    modules[4]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_System:
                if (modules.size() > 7 && modules[7]) { // SystemModule
                    modules[7]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_Clock:
                if (modules.size() > 0 && modules[0]) { // ClockModule
                    modules[0]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_Network:
                if (modules.size() > 6 && modules[6]) { // NetworkModule
                    modules[6]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_Notification:
                if (modules.size() > 9 && modules[9]) { // NotificationModule
                    modules[9]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_Screenshot:
                if (modules.size() > 8 && modules[8]) { // ScreenshotModule
                    modules[8]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            case IslandMode::Expanded_QuickActions:
                if (modules.size() > 10 && modules[10]) { // QuickActionModule
                    modules[10]->draw_expanded(cr, m_font_desc, config, width, height, hitboxes);
                }
                break;
            default:
                break;
        }
    }
}

} // namespace di
