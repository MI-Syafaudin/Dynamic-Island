#pragma once

#include "module_base.hpp"
#include <ctime>

namespace di {

class ClockModule : public ModuleBase {
public:
    ClockModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    const std::string& get_time_str() const { return m_time_str; }
    const std::string& get_date_str() const { return m_date_str; }

private:
    std::string m_time_str;
    std::string m_full_time_str;
    std::string m_date_str;
    std::string m_uptime_str;

    void update_uptime();
};

} // namespace di
