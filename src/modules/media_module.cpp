#include "media_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

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

    if (m_status == "Stopped" || m_status.empty()) {
        m_title.clear();
        m_artist.clear();
        m_player_name.clear();
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

    // Query Player Name
    fp = popen("playerctl metadata --format '{{playerName}}' 2>/dev/null", "r");
    if (fp) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            std::string p = buffer;
            while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
            m_player_name = p;
        } else {
            m_player_name.clear();
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
    if (g_utf8_validate(display_title.c_str(), -1, nullptr)) {
        glong char_count = g_utf8_strlen(display_title.c_str(), -1);
        if (char_count > 16) {
            gchar* end_ptr = g_utf8_offset_to_pointer(display_title.c_str(), 14);
            display_title = std::string(display_title.c_str(), end_ptr - display_title.c_str()) + "..";
        }
    } else {
        if (display_title.length() > 16) {
            display_title = display_title.substr(0, 14) + "..";
        }
    }

    std::string icon = (m_status == "Playing") ? "󰝚 " : "󰝛 ";
    std::string text = icon + display_title;

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.95);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.75);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "media"});
    current_x += tw + 10;

    g_object_unref(layout);
}

void MediaModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // 1. Header Pill (Centered at top)
    std::string badge_text;
    if (m_status == "Playing") {
        if (!m_player_name.empty()) {
            std::string pname = m_player_name;
            std::transform(pname.begin(), pname.end(), pname.begin(), ::toupper);
            badge_text = "󰝚 " + pname + " • NOW PLAYING";
        } else {
            badge_text = "󰝚 NOW PLAYING";
        }
    } else if (m_status == "Paused") {
        if (!m_player_name.empty()) {
            std::string pname = m_player_name;
            std::transform(pname.begin(), pname.end(), pname.begin(), ::toupper);
            badge_text = "󰝛 " + pname + " • PAUSED";
        } else {
            badge_text = "󰝛 PAUSED";
        }
    } else {
        badge_text = "󰝚 MEDIA PLAYER";
    }

    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, badge_text.c_str(), -1);
    pango_layout_set_width(layout, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    int btw, bth;
    pango_layout_get_pixel_size(layout, &btw, &bth);

    int pill_padding_x = 12;
    int pill_w = btw + (pill_padding_x * 2);
    int pill_h = bth + 6;
    int pill_x = (w - pill_w) / 2;
    int pill_y = 10;
    double pill_r = pill_h / 2.0;

    // Pill background
    cairo_new_sub_path(cr);
    cairo_arc(cr, pill_x + pill_r, pill_y + pill_r, pill_r, M_PI / 2, 3 * M_PI / 2);
    cairo_arc(cr, pill_x + pill_w - pill_r, pill_y + pill_r, pill_r, -M_PI / 2, M_PI / 2);
    cairo_close_path(cr);

    if (m_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.15);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.12);
    }
    cairo_fill_preserve(cr);

    // Pill 1px border
    if (m_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.35);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.25);
    }
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Pill text
    if (m_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    }
    cairo_move_to(cr, pill_x + pill_padding_x, pill_y + (pill_h - bth) / 2.0);
    pango_cairo_show_layout(cr, layout);

    // 2. Title (Bold, Centered)
    PangoFontDescription* bold_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold_desc);

    std::string disp_title = m_title.empty() ? "No Media Playing" : m_title;
    pango_layout_set_text(layout, disp_title.c_str(), -1);
    pango_layout_set_width(layout, (w - 40) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
    cairo_move_to(cr, 20, 36);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(bold_desc);

    // 3. Artist (Regular, Centered)
    pango_layout_set_font_description(layout, font_desc);
    std::string disp_artist = m_artist.empty() ? (m_title.empty() ? "Start playing music or video" : "Unknown Artist") : m_artist;
    pango_layout_set_text(layout, disp_artist.c_str(), -1);
    pango_layout_set_width(layout, (w - 40) * PANGO_SCALE);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.85);
    cairo_move_to(cr, 20, 58);
    pango_cairo_show_layout(cr, layout);

    // 4. Playback Controls Row: Prev, Play/Pause, Next
    int btn_h = 28;
    int btn_y = 86;
    int center_x = w / 2;

    int play_w = 46;
    int nav_w = 38;
    int gap = 16;

    int play_x = center_x - (play_w / 2);
    int prev_x = play_x - gap - nav_w;
    int next_x = play_x + play_w + gap;

    auto draw_button = [&](int bx, int bw, double r, const std::string& icon, const std::string& action, bool is_primary) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, bx + r, btn_y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + bw - r, btn_y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, bx + bw - r, btn_y + btn_h - r, r, 0, M_PI / 2);
        cairo_arc(cr, bx + r, btn_y + btn_h - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);

        if (is_primary && m_status == "Playing") {
            // Vibrant accent button for active play/pause
            cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.95);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        } else {
            // Sleek dark frosted button
            cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, 0.85);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.35, 0.35, 0.38, 0.5);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        pango_layout_set_width(layout, -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        pango_layout_set_text(layout, icon.c_str(), -1);

        int ic_w, ic_h;
        pango_layout_get_pixel_size(layout, &ic_w, &ic_h);

        if (is_primary && m_status == "Playing") {
            // Dark icon on light/accent button for maximum contrast
            cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 1.0);
        } else {
            cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        }

        cairo_move_to(cr, bx + (bw - ic_w) / 2.0, btn_y + (btn_h - ic_h) / 2.0);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({bx, btn_y, bw, btn_h, action, ""});
    };

    draw_button(prev_x, nav_w, 10.0, "󰒮", "media_prev", false);
    draw_button(play_x, play_w, 14.0, (m_status == "Playing" ? "󰏤" : "󰐊"), "media_toggle", true);
    draw_button(next_x, nav_w, 10.0, "󰒭", "media_next", false);

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
