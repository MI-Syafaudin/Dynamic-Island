#pragma once

#include "module_base.hpp"

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

    void play_pause();
    void next_track();
    void prev_track();

private:
    std::string m_status = "Stopped";
    std::string m_title;
    std::string m_artist;

    void query_mpris();
};

} // namespace di
