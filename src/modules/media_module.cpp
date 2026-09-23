#include "media_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace di {

MediaModule::MediaModule() {
    query_mpris();
}

void MediaModule::query_mpris() {
    // Query status
    FILE* fp = popen("playerctl status 2>/dev/null", "r");
    if (fp) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            std::string st = buffer;
            while (!st.empty() && (st.back() == '\n' || st.back() == '\r')) st.pop_back();
            m_status = st;
        } else {
            m_status = "Stopped";
        }
        pclose(fp);
    } else {
        m_status = "Stopped";
    }

    if (m_status == "Stopped") {
        m_title.clear();
        m_artist.clear();
        return;
    }

    // Query Title
    fp = popen("playerctl metadata title 2>/dev/null", "r");
    if (fp) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            std::string t = buffer;
            while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
            m_title = t;
        } else {
            m_title.clear();
        }
        pclose(fp);
    }

    // Query Artist
    fp = popen("playerctl metadata artist 2>/dev/null", "r");
    if (fp) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            std::string a = buffer;
            while (!a.empty() && (a.back() == '\n' || a.back() == '\r')) a.pop_back();
            m_artist = a;
        } else {
            m_artist.clear();
        }
        pclose(fp);
    }
}

void MediaModule::update() {
    query_mpris();
}

void MediaModule::play_pause() {
    std::system("playerctl play-pause 2>/dev/null &");
    query_mpris();
}

void MediaModule::next_track() {
    std::system("playerctl next 2>/dev/null &");
    query_mpris();
}

void MediaModule::prev_track() {
    std::system("playerctl previous 2>/dev/null &");
    query_mpris();
}

void MediaModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    if (m_title.empty()) return;

    std::string display_title = m_title;
    if (display_title.length() > 16) {
        display_title = display_title.substr(0, 14) + "..";
    }
    std::string text = "󰝚 " + display_title;

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.95);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.7);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "media"});
    current_x += tw + 10;

    g_object_unref(layout);
}

void MediaModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Header: 󰝚 Now Playing
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, "󰝚 Now Playing", -1);
    cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // Title (bold)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string disp_title = m_title.empty() ? "No Media Playing" : m_title;
    if (disp_title.length() > 32) disp_title = disp_title.substr(0, 30) + "...";
    pango_layout_set_text(layout, disp_title.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
    cairo_move_to(cr, 20, 28);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // Artist
    pango_layout_set_font_description(layout, font_desc);
    std::string disp_artist = m_artist.empty() ? "Unknown Artist" : m_artist;
    if (disp_artist.length() > 36) disp_artist = disp_artist.substr(0, 34) + "...";
    pango_layout_set_text(layout, disp_artist.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, 20, 46);
    pango_cairo_show_layout(cr, layout);

    // Playback Controls Row: Prev, Play/Pause, Next
    int btn_w = 40;
    int btn_h = 24;
    int btn_y = h - 28;
    int center_x = w / 2;

    auto draw_button = [&](int bx, const std::string& icon, const std::string& action) {
        cairo_new_sub_path(cr);
        double r = 6.0;
        cairo_arc(cr, bx + r, btn_y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + btn_w - r, btn_y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, bx + btn_w - r, btn_y + btn_h - r, r, 0, M_PI / 2);
        cairo_arc(cr, bx + r, btn_y + btn_h - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, 0.22, 0.22, 0.24, 0.9);
        cairo_fill(cr);

        pango_layout_set_text(layout, icon.c_str(), -1);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + (btn_h - th) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({bx, btn_y, btn_w, btn_h, action, ""});
    };

    draw_button(center_x - btn_w - 15, "󰒮", "media_prev");
    draw_button(center_x - (btn_w / 2), (m_status == "Playing" ? "󰏤" : "󰐊"), "media_toggle");
    draw_button(center_x + 15, "󰒭", "media_next");

    g_object_unref(layout);
}

bool MediaModule::handle_click(const std::string& action, const std::string& param) {
    if (action == "media_prev") {
        prev_track();
        return true;
    }
    if (action == "media_toggle") {
        play_pause();
        return true;
    }
    if (action == "media_next") {
        next_track();
        return true;
    }
    return false;
}

} // namespace di
