#pragma once

#include "module_base.hpp"
#include <ctime>
#include <string>
#include <vector>
#include <chrono>

namespace di {

struct AlarmItem {
    int id = 0;
    int hour = -1;       // 0-23, or -1 if timer
    int minute = -1;     // 0-59, or -1 if timer
    std::string label;   // e.g. "Bangun", "Meeting", "Timer (5m)"
    std::chrono::system_clock::time_point target_time;
    bool is_timer = false;
    int total_seconds = 0;
    bool is_ringing = false;
    std::chrono::steady_clock::time_point ring_start_time;
};

class ClockModule : public ModuleBase {
public:
    ClockModule();
    void update() override { update(false); }
    void update(bool expanded);
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    void draw_alarm_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;
    void handle_scroll(double x, double y, double delta);

    // Custom time setting API
    void adjust_hour(int delta);
    void adjust_minute(int delta);
    int get_selected_hour() const { return m_set_hour; }
    int get_selected_minute() const { return m_set_minute; }
    void set_selected_time(int hour, int minute);

    const std::string& get_time_str() const { return m_time_str; }
    const std::string& get_date_str() const { return m_date_str; }
    const std::string& get_full_time_str() const { return m_full_time_str; }

    // Alarm & Timer API
    int add_alarm(int hour, int minute, const std::string& label);
    int add_timer(int seconds, const std::string& label);
    bool cancel_alarm(int id); // id = -1 cancels all
    void dismiss_alarm();
    void snooze_alarm(int minutes = 5);

    const std::vector<AlarmItem>& get_alarms() const { return m_alarms; }
    const AlarmItem* get_ringing_alarm() const;
    const AlarmItem* get_next_alarm() const;
    bool check_alarms(AlarmItem& out_triggered);
    std::string get_alarms_list_str() const;

    bool has_active_timer() const;
    int get_remaining_timer_seconds() const;
    bool is_any_alarm_ringing() const;

private:
    std::string m_time_str;
    std::string m_full_time_str;
    std::string m_date_str;
    std::string m_uptime_str;
    int m_last_minute = -1;

    int m_next_alarm_id = 1;
    std::vector<AlarmItem> m_alarms;

    // Custom time setting state
    int m_set_hour = 7;
    int m_set_minute = 0;
    int m_hour_box_x = 0;
    int m_hour_box_w = 0;
    int m_min_box_x = 0;
    int m_min_box_w = 0;
    int m_stepper_y = 0;
    int m_stepper_h = 0;

    void update_uptime();
    void save_alarms() const;
    void load_alarms();
    std::string get_alarms_file_path() const;
};

} // namespace di
