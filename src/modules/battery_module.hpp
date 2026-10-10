#pragma once

#include "module_base.hpp"
#include <chrono>
#include <string>

namespace di {

class BatteryModule : public ModuleBase {
public:
    BatteryModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    void get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    int get_percentage() const { return m_percentage; }
    bool is_charging() const { return m_charging; }
    bool is_available() const { return m_available; }

    // Critical battery detection (returns true if a critical notification should trigger)
    bool check_critical_alert(int& out_level, std::string& out_msg);

    // Alarm controls
    bool is_alarm_active() const { return m_alarm_active; }
    void dismiss_alarm();
    void snooze_alarm(int minutes = 5);
    bool is_snoozed() const;
    void trigger_test_alert(int level = 5);

private:
    std::string m_bat_path;
    int m_percentage = 100;
    bool m_charging = false;
    bool m_available = false;

    // Critical notification state tracking
    int m_last_notified_tier = 0; // 0: normal, 1: low (<=20), 2: critical (<=10), 3: emergency (<=5)
    std::chrono::steady_clock::time_point m_last_notif_time;

    // Alarm state
    bool m_alarm_active = false;
    bool m_is_snoozed = false;
    std::chrono::steady_clock::time_point m_snooze_until;
    bool m_test_mode = false;
    int m_test_level = 5;
    int m_alert_level = 0;

    void detect_battery_path();
    void read_battery_sysfs();
};

} // namespace di
