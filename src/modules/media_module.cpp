#include "media_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <unistd.h>
#include <cctype>

namespace di {

MediaModule::MediaModule() {
    m_anim_start_time = std::chrono::steady_clock::now();
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

    if (m_cover_surface) {
        cairo_surface_destroy(m_cover_surface);
        m_cover_surface = nullptr;
    }
}

void MediaModule::on_expanded() {
    m_anim_start_time = std::chrono::steady_clock::now();
}

void MediaModule::worker_loop() {
    query_mpris();
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

cairo_surface_t* MediaModule::pixbuf_to_cairo_surface(GdkPixbuf* pixbuf) {
    if (!pixbuf) return nullptr;
    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return nullptr;
    }

    unsigned char* dst_data = cairo_image_surface_get_data(surface);
    int dst_stride = cairo_image_surface_get_stride(surface);
    cairo_surface_flush(surface);

    for (int y = 0; y < height; ++y) {
        const guchar* src = pixels + y * rowstride;
        uint32_t* dst = reinterpret_cast<uint32_t*>(dst_data + y * dst_stride);
        for (int x = 0; x < width; ++x) {
            uint32_t r = src[0];
            uint32_t g = src[1];
            uint32_t b = src[2];
            uint32_t a = (n_channels == 4) ? src[3] : 255;
            src += n_channels;

            if (a == 0) {
                dst[x] = 0;
            } else if (a == 255) {
                dst[x] = (255u << 24) | (r << 16) | (g << 8) | b;
            } else {
                uint32_t pr = (r * a + 127) / 255;
                uint32_t pg = (g * a + 127) / 255;
                uint32_t pb = (b * a + 127) / 255;
                dst[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
            }
        }
    }

    cairo_surface_mark_dirty(surface);
    return surface;
}

void MediaModule::detect_platform(const std::string& player_name, const std::string& url, const std::string& art_url) {
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    std::string l_pname = to_lower(player_name);
    std::string l_url = to_lower(url);
    std::string l_art = to_lower(art_url);

    // 1. YouTube Music: URL contains music.youtube.com
    if (l_url.find("music.youtube.com") != std::string::npos) {
        m_platform_name = "YouTube Music";
        m_platform_icon = "󰠃";
        m_platform_color = ColorRGBA::from_hex("#FF0033");
        return;
    }

    // 2. YouTube: URL contains youtube.com or youtu.be
    if (l_url.find("youtube.com") != std::string::npos || l_url.find("youtu.be") != std::string::npos) {
        m_platform_name = "YouTube";
        m_platform_icon = "";
        m_platform_color = ColorRGBA::from_hex("#FF0000");
        return;
    }

    // 3. Spotify: playerName, URL, or artUrl
    if (l_pname.find("spotify") != std::string::npos ||
        l_url.find("spotify.com") != std::string::npos ||
        l_art.find("scdn.co") != std::string::npos) {
        m_platform_name = "Spotify";
        m_platform_icon = "";
        m_platform_color = ColorRGBA::from_hex("#1DB954");
        return;
    }

    // 4. Apple Music: URL or artUrl
    if (l_url.find("music.apple.com") != std::string::npos ||
        l_art.find("mzstatic.com") != std::string::npos) {
        m_platform_name = "Apple Music";
        m_platform_icon = "󰀥";
        m_platform_color = ColorRGBA::from_hex("#FA243C");
        return;
    }

    // 5. Tidal: playerName or URL
    if (l_pname.find("tidal") != std::string::npos || l_url.find("tidal.com") != std::string::npos) {
        m_platform_name = "Tidal";
        m_platform_icon = "󰎆";
        m_platform_color = ColorRGBA::from_hex("#00E5FF");
        return;
    }

    // 6. Deezer: playerName, URL, or artUrl
    if (l_pname.find("deezer") != std::string::npos ||
        l_url.find("deezer.com") != std::string::npos ||
        l_art.find("dzcdn.net") != std::string::npos) {
        m_platform_name = "Deezer";
        m_platform_icon = "󰝚";
        m_platform_color = ColorRGBA::from_hex("#A238FF");
        return;
    }

    // 7. SoundCloud: URL or artUrl
    if (l_url.find("soundcloud.com") != std::string::npos ||
        l_art.find("sndcdn.com") != std::string::npos) {
        m_platform_name = "SoundCloud";
        m_platform_icon = "󰓇";
        m_platform_color = ColorRGBA::from_hex("#FF5500");
        return;
    }

    // 8. VLC / MPV / Local Players
    if (l_pname.find("vlc") != std::string::npos) {
        m_platform_name = "VLC";
        m_platform_icon = "󰕼";
        m_platform_color = ColorRGBA::from_hex("#FF8800");
        return;
    }
    if (l_pname.find("mpv") != std::string::npos) {
        m_platform_name = "MPV";
        m_platform_icon = "";
        m_platform_color = ColorRGBA::from_hex("#663399");
        return;
    }
    if (l_pname.find("amberol") != std::string::npos) {
        m_platform_name = "Amberol";
        m_platform_icon = "󰝚";
        m_platform_color = ColorRGBA::from_hex("#F39C12");
        return;
    }
    if (l_pname.find("elisa") != std::string::npos) {
        m_platform_name = "Elisa";
        m_platform_icon = "󰝚";
        m_platform_color = ColorRGBA::from_hex("#1ABC9C");
        return;
    }
    if (l_pname.find("rhythmbox") != std::string::npos) {
        m_platform_name = "Rhythmbox";
        m_platform_icon = "󰝚";
        m_platform_color = ColorRGBA::from_hex("#E67E22");
        return;
    }
    if (l_pname.find("feishin") != std::string::npos) {
        m_platform_name = "Feishin";
        m_platform_icon = "󰝚";
        m_platform_color = ColorRGBA::from_hex("#3498DB");
        return;
    }

    // 9. Fallback Browser / Generic Web
    if (l_pname.find("firefox") != std::string::npos) {
        m_platform_name = "Firefox";
        m_platform_icon = "󰈹";
        m_platform_color = ColorRGBA::from_hex("#FF7139");
        return;
    }
    if (l_pname.find("librewolf") != std::string::npos) {
        m_platform_name = "LibreWolf";
        m_platform_icon = "󰈹";
        m_platform_color = ColorRGBA::from_hex("#3274D9");
        return;
    }
    if (l_pname.find("zen") != std::string::npos) {
        m_platform_name = "Zen Browser";
        m_platform_icon = "󰈹";
        m_platform_color = ColorRGBA::from_hex("#E06C75");
        return;
    }
    if (l_pname.find("chrom") != std::string::npos) {
        m_platform_name = "Chrome";
        m_platform_icon = "󰊯";
        m_platform_color = ColorRGBA::from_hex("#4285F4");
        return;
    }
    if (l_pname.find("brave") != std::string::npos) {
        m_platform_name = "Brave";
        m_platform_icon = "󰊯";
        m_platform_color = ColorRGBA::from_hex("#FB542B");
        return;
    }

    // 10. Generic fallback
    if (!player_name.empty()) {
        std::string name = player_name;
        name[0] = std::toupper(static_cast<unsigned char>(name[0]));
        m_platform_name = name;
    } else {
        m_platform_name = "Media Player";
    }
    m_platform_icon = "󰝚";
    m_platform_color = ColorRGBA::from_hex("#00A8FF");
}

void MediaModule::load_cover_art_worker(const std::string& art_url) {
    if (art_url.empty()) {
        cairo_surface_t* old_surf = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            old_surf = m_cover_surface;
            m_cover_surface = nullptr;
        }
        if (old_surf) cairo_surface_destroy(old_surf);
        return;
    }

    GdkPixbuf* pixbuf = nullptr;
    GError* err = nullptr;

    if (art_url.rfind("file://", 0) == 0) {
        gchar* filename = g_filename_from_uri(art_url.c_str(), nullptr, nullptr);
        std::string local_path = filename ? filename : art_url.substr(7);
        if (filename) g_free(filename);
        pixbuf = gdk_pixbuf_new_from_file_at_scale(local_path.c_str(), 88, 88, FALSE, &err);
    } else if (art_url.rfind("http://", 0) == 0 || art_url.rfind("https://", 0) == 0) {
        std::string tmp_file = "/tmp/di_cover_" + std::to_string(getpid()) + ".img";
        std::string cmd = "curl -s -L -m 4 -o \"" + tmp_file + "\" \"" + art_url + "\" 2>/dev/null";
        int r = std::system(cmd.c_str());
        if (r == 0) {
            pixbuf = gdk_pixbuf_new_from_file_at_scale(tmp_file.c_str(), 88, 88, FALSE, &err);
        }
        unlink(tmp_file.c_str());
    } else if (art_url.rfind("data:image/", 0) == 0) {
        size_t comma = art_url.find(',');
        if (comma != std::string::npos) {
            std::string b64 = art_url.substr(comma + 1);
            gsize out_len = 0;
            guchar* data = g_base64_decode(b64.c_str(), &out_len);
            if (data && out_len > 0) {
                GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
                if (gdk_pixbuf_loader_write(loader, data, out_len, nullptr)) {
                    gdk_pixbuf_loader_close(loader, nullptr);
                    GdkPixbuf* orig = gdk_pixbuf_loader_get_pixbuf(loader);
                    if (orig) {
                        pixbuf = gdk_pixbuf_scale_simple(orig, 88, 88, GDK_INTERP_BILINEAR);
                    }
                } else {
                    gdk_pixbuf_loader_close(loader, nullptr);
                }
                g_object_unref(loader);
                g_free(data);
            }
        }
    } else if (!art_url.empty() && art_url[0] == '/') {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(art_url.c_str(), 88, 88, FALSE, &err);
    }

    if (err) {
        g_error_free(err);
    }

    cairo_surface_t* new_surf = nullptr;
    if (pixbuf) {
        new_surf = pixbuf_to_cairo_surface(pixbuf);
        g_object_unref(pixbuf);
    }

    cairo_surface_t* old_surf = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        old_surf = m_cover_surface;
        m_cover_surface = new_surf;
    }
    if (old_surf) {
        cairo_surface_destroy(old_surf);
    }
}

void MediaModule::query_mpris() {
    FILE* fp = popen("playerctl metadata --format '{{status}}:::{{playerName}}:::{{artist}}:::{{title}}:::{{xesam:url}}:::{{mpris:artUrl}}' 2>/dev/null", "r");
    if (!fp) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_title.empty()) {
            m_anim_start_time = std::chrono::steady_clock::now();
        }
        m_status = "Stopped";
        m_title.clear();
        m_artist.clear();
        m_player_name.clear();
        m_url.clear();
        m_art_url.clear();
        return;
    }

    char buffer[4096];
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
        m_url.clear();
        m_art_url.clear();
        return;
    }

    // Split format tokens: {{status}}:::{{playerName}}:::{{artist}}:::{{title}}:::{{xesam:url}}:::{{mpris:artUrl}}
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < line.size()) {
        size_t pos = line.find(":::", start);
        if (pos == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 3;
    }

    std::string status = parts.size() > 0 ? parts[0] : "";
    std::string player_name = parts.size() > 1 ? parts[1] : "";
    std::string artist = parts.size() > 2 ? parts[2] : "";
    std::string title = parts.size() > 3 ? parts[3] : "";
    std::string url = parts.size() > 4 ? parts[4] : "";
    std::string art_url = parts.size() > 5 ? parts[5] : "";

    if (status == "Stopped") {
        title.clear();
        artist.clear();
        player_name.clear();
        url.clear();
        art_url.clear();
    }

    detect_platform(player_name, url, art_url);

    // Only load cover art when art_url changes
    if (art_url != m_last_art_url) {
        load_cover_art_worker(art_url);
        m_last_art_url = art_url;
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
        m_url = url;
        m_art_url = art_url;
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
    std::string cur_icon;
    ColorRGBA cur_color;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cur_title = m_title;
        cur_status = m_status;
        cur_icon = m_platform_icon;
        cur_color = m_platform_color;
    }
    if (cur_title.empty() || cur_status == "Stopped") return;

    // In Idle/Compact mode, ONLY display the media/platform icon for a clean, short Dynamic Island bar
    std::string icon = cur_icon;
    if (cur_status == "Paused" && icon == "󰝚") {
        icon = "󰝛";
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, icon.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (cur_status == "Playing") {
        cairo_set_source_rgba(cr, cur_color.r, cur_color.g, cur_color.b, 1.0);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.75);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 3, y, tw + 6, h, "toggle_expand", "media"});
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
        // Left aligned in 2-column layout for clean modern hierarchy
        int draw_x = area_x;
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
    (void)h;
    std::string cur_status;
    std::string cur_player_name;
    std::string cur_title;
    std::string cur_artist;
    std::string cur_platform_name;
    std::string cur_platform_icon;
    ColorRGBA cur_platform_color;
    cairo_surface_t* cover = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cur_status = m_status;
        cur_player_name = m_player_name;
        cur_title = m_title;
        cur_artist = m_artist;
        cur_platform_name = m_platform_name;
        cur_platform_icon = m_platform_icon;
        cur_platform_color = m_platform_color;
        cover = m_cover_surface;
        if (cover) {
            cairo_surface_reference(cover);
        }
    }

    // Helper rounded rect
    auto draw_rrect = [](cairo_t* c, double x, double y, double rw, double rh, double r) {
        cairo_new_sub_path(c);
        cairo_arc(c, x + r, y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(c, x + rw - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(c, x + rw - r, y + rh - r, r, 0, M_PI / 2);
        cairo_arc(c, x + r, y + rh - r, r, M_PI / 2, M_PI);
        cairo_close_path(c);
    };

    // =========================================================================
    // 1. Kolom Kiri: Album Art / Vinyl Card (x = 18, y = 20, size = 88x88, r = 14)
    // =========================================================================
    const double art_x = 18.0;
    const double art_y = 20.0;
    const double art_size = 88.0;
    const double art_radius = 14.0;

    if (cover && cairo_surface_status(cover) == CAIRO_STATUS_SUCCESS) {
        // Soft drop shadow
        cairo_save(cr);
        draw_rrect(cr, art_x, art_y + 2.0, art_size, art_size, art_radius);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.40);
        cairo_fill(cr);
        cairo_restore(cr);

        // Clip rounded rect and paint scaled cover art
        cairo_save(cr);
        draw_rrect(cr, art_x, art_y, art_size, art_size, art_radius);
        cairo_clip(cr);

        cairo_set_source_surface(cr, cover, art_x, art_y);
        cairo_paint(cr);
        cairo_restore(cr);

        // Inner border highlight
        cairo_save(cr);
        draw_rrect(cr, art_x + 0.5, art_y + 0.5, art_size - 1.0, art_size - 1.0, art_radius - 0.5);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        cairo_restore(cr);
    } else {
        // Fallback: Elegant dark frosted vinyl disc card
        cairo_save(cr);
        draw_rrect(cr, art_x, art_y, art_size, art_size, art_radius);
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        // Concentric vinyl grooves
        double cx = art_x + (art_size / 2.0);
        double cy = art_y + (art_size / 2.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
        cairo_set_line_width(cr, 1.0);
        double groove_radii[] = {16.0, 24.0, 32.0, 38.0};
        for (double gr : groove_radii) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, gr, 0, 2 * M_PI);
            cairo_stroke(cr);
        }

        // Center disc hole
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 11.0, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 0.40);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        // Center platform icon
        PangoLayout* ic_layout = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(ic_layout, font_desc);
        pango_layout_set_text(ic_layout, cur_platform_icon.c_str(), -1);
        int icw, ich;
        pango_layout_get_pixel_size(ic_layout, &icw, &ich);
        cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 1.0);
        cairo_move_to(cr, cx - (icw / 2.0), cy - (ich / 2.0));
        pango_cairo_show_layout(cr, ic_layout);
        g_object_unref(ic_layout);

        cairo_restore(cr);
    }

    if (cover) {
        cairo_surface_destroy(cover);
        cover = nullptr;
    }

    // =========================================================================
    // 2. Kolom Kanan: Info Lagu & Kontrol (x_start = 122, width = w - 138)
    // =========================================================================
    const int right_x = 122;
    const int right_w = std::max(100, w - 138);

    // --- Baris 1 (y = 16): Platform Pill Badge ---
    std::string badge_pname = cur_platform_name;
    std::transform(badge_pname.begin(), badge_pname.end(), badge_pname.begin(), ::toupper);
    std::string badge_status = (cur_status == "Playing") ? "PLAYING" : ((cur_status == "Paused") ? "PAUSED" : "READY");
    std::string badge_text = cur_platform_icon + " " + badge_pname + " • " + badge_status;

    PangoLayout* badge_layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(badge_layout, font_desc);
    pango_layout_set_text(badge_layout, badge_text.c_str(), -1);

    int btw, bth;
    pango_layout_get_pixel_size(badge_layout, &btw, &bth);

    int pill_pad_x = 8;
    int pill_w = btw + (pill_pad_x * 2);
    int pill_h = bth + 4;
    int pill_x = right_x;
    int pill_y = 16;
    double pill_r = pill_h / 2.0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, pill_x + pill_r, pill_y + pill_r, pill_r, M_PI / 2, 3 * M_PI / 2);
    cairo_arc(cr, pill_x + pill_w - pill_r, pill_y + pill_r, pill_r, -M_PI / 2, M_PI / 2);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 0.16);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 0.40);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 1.0);
    cairo_move_to(cr, pill_x + pill_pad_x, pill_y + (pill_h - bth) / 2.0);
    pango_cairo_show_layout(cr, badge_layout);
    g_object_unref(badge_layout);

    // --- Baris 2 (y = 42): Judul Lagu (Bold) ---
    std::string disp_title = cur_title.empty() ? "No Media Playing" : cur_title;
    bool title_overflow = false;
    draw_marquee_text(cr, font_desc, disp_title, right_x, right_w, 42, config.colors.text, true, title_overflow);

    // --- Baris 3 (y = 63): Nama Artis (Subtext) ---
    std::string disp_artist = cur_artist.empty() ? (cur_title.empty() ? "Start playing music or video" : "Unknown Artist") : cur_artist;
    bool artist_overflow = false;
    ColorRGBA artist_color = config.colors.subtext;
    artist_color.a = 0.85;
    draw_marquee_text(cr, font_desc, disp_artist, right_x, right_w, 63, artist_color, false, artist_overflow);

    m_needs_marquee = (title_overflow || artist_overflow);

    // --- Baris 4 (y = 86): Modern Playback Controls ---
    const int btn_y = 86;
    const int btn_h = 28;
    const int prev_w = 38;
    const int play_w = 46;
    const int next_w = 38;
    const int gap = 12;

    int prev_x = right_x;
    int play_x = prev_x + prev_w + gap;
    int next_x = play_x + play_w + gap;

    auto draw_btn = [&](int bx, int bw, double r, const std::string& icon, const std::string& action, bool is_primary) {
        draw_rrect(cr, bx, btn_y, bw, btn_h, r);

        if (is_primary && cur_status == "Playing") {
            // Vibrant platform color button when active
            cairo_set_source_rgba(cr, cur_platform_color.r, cur_platform_color.g, cur_platform_color.b, 0.95);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        } else {
            // Sleek frosted dark button
            cairo_set_source_rgba(cr, 0.20, 0.20, 0.22, 0.85);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.35, 0.35, 0.38, 0.50);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        PangoLayout* ic_l = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(ic_l, font_desc);
        pango_layout_set_text(ic_l, icon.c_str(), -1);
        int cw, ch;
        pango_layout_get_pixel_size(ic_l, &cw, &ch);

        if (is_primary && cur_status == "Playing") {
            cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 1.0);
        } else {
            cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        }
        cairo_move_to(cr, bx + (bw - cw) / 2.0, btn_y + (btn_h - ch) / 2.0);
        pango_cairo_show_layout(cr, ic_l);
        g_object_unref(ic_l);

        hitboxes.push_back({bx, btn_y, bw, btn_h, action, ""});
    };

    draw_btn(prev_x, prev_w, 10.0, "󰒮", "media_prev", false);
    draw_btn(play_x, play_w, 14.0, (cur_status == "Playing" ? "󰏤" : "󰐊"), "media_toggle", true);
    draw_btn(next_x, next_w, 10.0, "󰒭", "media_next", false);
}

bool MediaModule::handle_click(const std::string& action, const std::string& param) {
    (void)param;
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
