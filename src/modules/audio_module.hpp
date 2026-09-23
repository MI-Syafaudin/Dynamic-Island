#pragma once

#include "module_base.hpp"

namespace di {

class AudioModule : public ModuleBase {
public:
    AudioModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    int get_volume() const { return m_volume; }
    bool is_muted() const { return m_muted; }

    void change_volume(int delta_percent);
    void toggle_mute();

private:
    int m_volume = 50;
    bool m_muted = false;
    void query_audio_state();
};

} // namespace di
