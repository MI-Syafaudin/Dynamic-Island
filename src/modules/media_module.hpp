#pragma once

#include "module_base.hpp"
#include <chrono>

namespace di {

class MediaModule : public ModuleBase {
public:
    MediaModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    bool is_playing() const { return m_status == "Playing"; }
    bool has_media() const { return !m_title.empty(); }
    const std::string& get_status() const { return m_status; }
    const std::string& get_title() const { return m_title; }
    const std::string& get_artist() const { return m_artist; }

    void play_pause();
    void next_track();
    void prev_track();

    bool needs_marquee_animation() const { return m_needs_marquee; }
    void on_expanded();

private:
    std::string m_status = "Stopped";
    std::string m_title;
    std::string m_artist;
    std::string m_player_name;

    std::string m_last_title;
    std::string m_last_artist;
    std::chrono::steady_clock::time_point m_anim_start_time;
    bool m_needs_marquee = false;

    void query_mpris();
    void draw_marquee_text(
        cairo_t* cr,
        PangoFontDescription* font_desc,
        const std::string& text,
        int area_x,
        int area_w,
        int y,
        const ColorRGBA& color,
        bool is_bold,
        bool& out_overflow
    );
};

} // namespace di
