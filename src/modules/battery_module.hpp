#pragma once

#include "module_base.hpp"

namespace di {

class BatteryModule : public ModuleBase {
public:
    BatteryModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;

    int get_percentage() const { return m_percentage; }
    bool is_charging() const { return m_charging; }

private:
    std::string m_bat_path;
    int m_percentage = 100;
    bool m_charging = false;
    bool m_available = false;

    void detect_battery_path();
    void read_battery_sysfs();
};

} // namespace di
