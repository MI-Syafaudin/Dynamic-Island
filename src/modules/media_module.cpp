#include "media_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace di {

MediaModule::MediaModule() {
    m_anim_start_time = std::chrono::steady_clock::now();
    query_mpris();
    m_worker_running = true;
    m_worker_thread = std::thread(&MediaModule::worker_loop, this);
}

MediaModule::~MediaModule() {
    {
        std::lock_guard<std::mutex> lock(m_worker_mutex);
        m_worker_running = false;
    }
    m_worker_cv.notify_all();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

void MediaModule::on_expanded() {
    m_anim_start_time = std::chrono::steady_clock::now();
}

void MediaModule::worker_loop() {
    while (m_worker_running) {
        int wait_ms = 1500;
        {
            std::unique_lock<std::mutex> lock(m_worker_mutex);
            m_worker_cv.wait_for(lock, std::chrono::milliseconds(wait_ms), [this]() {
                return !m_worker_running || m_request_query.load();
            });
            if (!m_worker_running) break;
            m_request_query.store(false);
        }
        query_mpris();
    }
}

void MediaModule::trigger_async_query() {
    m_request_query.store(true);
    m_worker_cv.notify_one();
}

void MediaModule::query_mpris() {
    FILE* fp = popen("playerctl metadata --format '{{status}}:::{{playerName}}:::{{artist}}:::{{title}}' 2>/dev/null", "r");
    if (!fp) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_title.empty()) {
            m_anim_start_time = std::chrono::steady_clock::now();
        }
        m_status = "Stopped";
        m_title.clear();
        m_artist.clear();
        m_player_name.clear();
        return;
    }

    char buffer[512];
    std::string line;
    if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    }
    pclose(fp);

    if (line.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_title.empty()) {
            m_anim_start_time = std::chrono::steady_clock::now();
        }
        m_status = "Stopped";
        m_title.clear();
        m_artist.clear();
        m_player_name.clear();
        return;
    }

    std::string status;
    std::string player_name;
    std::string artist;
    std::string title;

    size_t p1 = line.find(":::");
    if (p1 == std::string::npos) {
        status = line;
    } else {
        status = line.substr(0, p1);
        size_t p2 = line.find(":::", p1 + 3);
        if (p2 == std::string::npos) {
            player_name = line.substr(p1 + 3);
        } else {
            player_name = line.substr(p1 + 3, p2 - (p1 + 3));
            size_t p3 = line.find(":::", p2 + 3);
            if (p3 == std::string::npos) {
                artist = line.substr(p2 + 3);
            } else {
                artist = line.substr(p2 + 3, p3 - (p2 + 3));
                title = line.substr(p3 + 3);
            }
        }
    }

    if (status == "Stopped") {
        title.clear();
        artist.clear();
        player_name.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_title != title || m_artist != artist) {
            m_anim_start_time = std::chrono::steady_clock::now();
        }
        m_status = status;
        m_player_name = player_name;
        m_artist = artist;
        m_title = title;
    }
}

void MediaModule::update() {
    trigger_async_query();
}

void MediaModule::play_pause() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status == "Playing") m_status = "Paused";
        else if (m_status == "Paused") m_status = "Playing";
    }
    std::system("playerctl play-pause 2>/dev/null &");
    trigger_async_query();
}

void MediaModule::next_track() {
    std::system("playerctl next 2>/dev/null &");
    trigger_async_query();
}

void MediaModule::prev_track() {
    std::system("playerctl previous 2>/dev/null &");
    trigger_async_query();
}

void MediaModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    std::string cur_title;
    std::string cur_status;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cur_title = m_title;
        cur_status = m_status;
    }
    if (cur_title.empty()) return;

    std::string display_title = cur_title;
    int max_len = config.max_media_title_length > 0 ? config.max_media_title_length : 28;
    if (g_utf8_validate(display_title.c_str(), -1, nullptr)) {
        glong char_count = g_utf8_strlen(display_title.c_str(), -1);
        if (char_count > max_len) {
            gchar* end_ptr = g_utf8_offset_to_pointer(display_title.c_str(), std::max(1, max_len - 2));
            display_title = std::string(display_title.c_str(), end_ptr - display_title.c_str()) + "..";
        }
    } else {
        if (static_cast<int>(display_title.length()) > max_len) {
            display_title = display_title.substr(0, std::max(1, max_len - 2)) + "..";
        }
    }

    std::string icon = (cur_status == "Playing") ? "󰝚 " : "󰝛 ";
    std::string text = icon + display_title;

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, text.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (cur_status == "Playing") {
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

void MediaModule::draw_marquee_text(
    cairo_t* cr,
    PangoFontDescription* font_desc,
    const std::string& text,
    int area_x,
    int area_w,
    int y,
    const ColorRGBA& color,
    bool is_bold,
    bool& out_overflow
) {
    if (text.empty() || area_w <= 0) {
        out_overflow = false;
        return;
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* active_desc = font_desc;
    PangoFontDescription* bold_desc = nullptr;
    if (is_bold) {
        bold_desc = pango_font_description_copy(font_desc);
        pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);
        active_desc = bold_desc;
    }
    pango_layout_set_font_description(layout, active_desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    if (text_w <= area_w) {
        out_overflow = false;
        int draw_x = area_x + (area_w - text_w) / 2;
        cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
        cairo_move_to(cr, draw_x, y);
        pango_cairo_show_layout(cr, layout);
    } else {
        out_overflow = true;
        double overflow = static_cast<double>(text_w - area_w);

        const double speed = 36.0;         // pixels per second forward
        const double return_speed = 48.0;  // pixels per second return
        const double pause_start = 2.2;    // seconds pause at start
        const double pause_end = 1.8;      // seconds pause at end

        double t_scroll = overflow / speed;
        double t_return = overflow / return_speed;
        double t_cycle = pause_start + t_scroll + pause_end + t_return;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - m_anim_start_time).count();
        double t_rel = std::fmod(elapsed, t_cycle);
        if (t_rel < 0) t_rel += t_cycle;

        double offset = 0.0;
        if (t_rel < pause_start) {
            offset = 0.0;
        } else if (t_rel < pause_start + t_scroll) {
            double p = (t_rel - pause_start) / t_scroll;
            double ease = 0.5 - 0.5 * std::cos(p * M_PI);
            offset = ease * overflow;
        } else if (t_rel < pause_start + t_scroll + pause_end) {
            offset = overflow;
        } else {
            double p = (t_rel - (pause_start + t_scroll + pause_end)) / t_return;
            double ease = 0.5 - 0.5 * std::cos(p * M_PI);
            offset = (1.0 - ease) * overflow;
        }

        cairo_save(cr);
        cairo_rectangle(cr, area_x, y - 2, area_w, text_h + 4);
        cairo_clip(cr);

        cairo_push_group(cr);
        cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
        cairo_move_to(cr, area_x - offset, y);
        pango_cairo_show_layout(cr, layout);
        cairo_pop_group_to_source(cr);

        double fade_w = 16.0;
        double left_frac = std::clamp(fade_w / area_w, 0.01, 0.45);
        double right_frac = std::clamp(1.0 - (fade_w / area_w), 0.55, 0.99);

        cairo_pattern_t* mask = cairo_pattern_create_linear(area_x, 0, area_x + area_w, 0);
        cairo_pattern_add_color_stop_rgba(mask, 0.0, 0, 0, 0, (offset > 2.0 ? 0.0 : 1.0));
        cairo_pattern_add_color_stop_rgba(mask, left_frac, 0, 0, 0, 1.0);
        cairo_pattern_add_color_stop_rgba(mask, right_frac, 0, 0, 0, 1.0);
        cairo_pattern_add_color_stop_rgba(mask, 1.0, 0, 0, 0, (offset < (overflow - 2.0) ? 0.0 : 1.0));

        cairo_mask(cr, mask);
        cairo_pattern_destroy(mask);

        cairo_restore(cr);
    }

    if (bold_desc) {
        pango_font_description_free(bold_desc);
    }
    g_object_unref(layout);
}

void MediaModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    std::string cur_status;
    std::string cur_player_name;
    std::string cur_title;
    std::string cur_artist;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cur_status = m_status;
        cur_player_name = m_player_name;
        cur_title = m_title;
        cur_artist = m_artist;
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);

    // 1. Header Pill (Centered at top)
    std::string badge_text;
    if (cur_status == "Playing") {
        if (!cur_player_name.empty()) {
            std::string pname = cur_player_name;
            std::transform(pname.begin(), pname.end(), pname.begin(), ::toupper);
            badge_text = "󰝚 " + pname + " • NOW PLAYING";
        } else {
            badge_text = "󰝚 NOW PLAYING";
        }
    } else if (cur_status == "Paused") {
        if (!cur_player_name.empty()) {
            std::string pname = cur_player_name;
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

    if (cur_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.15);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.12);
    }
    cairo_fill_preserve(cr);

    // Pill 1px border
    if (cur_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.35);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.25);
    }
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Pill text
    if (cur_status == "Playing") {
        cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    }
    cairo_move_to(cr, pill_x + pill_padding_x, pill_y + (pill_h - bth) / 2.0);
    pango_cairo_show_layout(cr, layout);

    // 2. Title (Bold, Centered or Smooth Ping-Pong Marquee)
    std::string disp_title = cur_title.empty() ? "No Media Playing" : cur_title;
    bool title_overflow = false;
    draw_marquee_text(cr, font_desc, disp_title, 20, w - 40, 36, config.colors.text, true, title_overflow);

    // 3. Artist (Regular, Centered or Smooth Ping-Pong Marquee)
    std::string disp_artist = cur_artist.empty() ? (cur_title.empty() ? "Start playing music or video" : "Unknown Artist") : cur_artist;
    bool artist_overflow = false;
    ColorRGBA artist_color = config.colors.subtext;
    artist_color.a = 0.85;
    draw_marquee_text(cr, font_desc, disp_artist, 20, w - 40, 58, artist_color, false, artist_overflow);

    m_needs_marquee = (title_overflow || artist_overflow);

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

        if (is_primary && cur_status == "Playing") {
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

        if (is_primary && cur_status == "Playing") {
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
    draw_button(play_x, play_w, 14.0, (cur_status == "Playing" ? "󰏤" : "󰐊"), "media_toggle", true);
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
