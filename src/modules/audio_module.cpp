#include "audio_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <algorithm>

namespace di {

AudioModule::AudioModule() {
    query_audio_state();
}

void AudioModule::query_audio_state() {
    FILE* fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!fp) return;

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        result += buffer;
    }
    pclose(fp);

    // Format: "Volume: 0.75" or "Volume: 0.75 [MUTED]"
    m_muted = (result.find("[MUTED]") != std::string::npos);

    size_t pos = result.find("Volume:");
    if (pos != std::string::npos) {
        float vol = 0.0f;
        if (sscanf(result.c_str() + pos + 7, "%f", &vol) == 1) {
            m_volume = std::round(vol * 100.0f);
        }
    }
}

void AudioModule::update() {
    query_audio_state();
}

void AudioModule::change_volume(int delta_percent) {
    std::stringstream ss;
    if (delta_percent >= 0) {
        ss << "wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ " << delta_percent << "%+ 2>/dev/null";
    } else {
        ss << "wpctl set-volume @DEFAULT_AUDIO_SINK@ " << (-delta_percent) << "%- 2>/dev/null";
    }
    std::system(ss.str().c_str());
    query_audio_state();
}

void AudioModule::toggle_mute() {
    std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle 2>/dev/null");
    query_audio_state();
}

void AudioModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    std::string icon = m_muted ? "󰖁" : (m_volume > 50 ? "󰕾" : (m_volume > 0 ? "󰖀" : "󰕿"));
    std::string text = icon + " " + std::to_string(m_volume) + "%";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_muted) {
        cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.9);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "audio_toggle_mute", ""});
    current_x += tw + 10;

    g_object_unref(layout);
}

void AudioModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Title / Icon
    std::string icon = m_muted ? "󰖁" : "󰕾";
    std::string title = icon + (m_muted ? " Audio Muted" : " Volume");
    pango_layout_set_text(layout, title.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, m_muted ? config.colors.danger.r : config.colors.text.r,
                              m_muted ? config.colors.danger.g : config.colors.text.g,
                              m_muted ? config.colors.danger.b : config.colors.text.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // Percentage text on right
    std::string pct_text = std::to_string(m_volume) + "%";
    pango_layout_set_text(layout, pct_text.c_str(), -1);
    int ptw, pth;
    pango_layout_get_pixel_size(layout, &ptw, &pth);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 1.0);
    cairo_move_to(cr, w - 20 - ptw, 10);
    pango_cairo_show_layout(cr, layout);

    // Horizontal slider track
    int bar_x = 20;
    int bar_y = 30;
    int bar_w = w - 40;
    int bar_h = 8;
    double bar_r = 4.0;

    // Track background
    cairo_new_sub_path(cr);
    cairo_arc(cr, bar_x + bar_r, bar_y + bar_r, bar_r, M_PI / 2, 3 * M_PI / 2);
    cairo_arc(cr, bar_x + bar_w - bar_r, bar_y + bar_r, bar_r, -M_PI / 2, M_PI / 2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.22, 0.9);
    cairo_fill(cr);

    // Fill bar
    double clamped_vol = std::clamp(m_volume, 0, 100) / 100.0;
    int fill_w = std::max(static_cast<int>(bar_w * clamped_vol), 8);

    if (clamped_vol > 0.0) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, bar_x + bar_r, bar_y + bar_r, bar_r, M_PI / 2, 3 * M_PI / 2);
        cairo_arc(cr, bar_x + fill_w - bar_r, bar_y + bar_r, bar_r, -M_PI / 2, M_PI / 2);
        cairo_close_path(cr);

        if (m_muted) {
            cairo_set_source_rgba(cr, config.colors.danger.r, config.colors.danger.g, config.colors.danger.b, 0.85);
        } else {
            cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 0.95);
        }
        cairo_fill(cr);
    }

    g_object_unref(layout);

    // Hitbox to toggle mute on click
    hitboxes.push_back({0, 0, w, h, "audio_toggle_mute", ""});
}

bool AudioModule::handle_click(const std::string& action, const std::string& param) {
    if (action == "audio_toggle_mute") {
        toggle_mute();
        return true;
    }
    return false;
}

} // namespace di
