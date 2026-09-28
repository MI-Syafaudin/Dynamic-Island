#pragma once

#include "module_base.hpp"

namespace di {

class QuickActionModule : public ModuleBase {
public:
    QuickActionModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

private:
    bool m_wifi_active = false;
    bool m_bt_active = false;
    bool m_mute_active = false;
    bool m_night_active = false;

    void query_states();
};

} // namespace di
