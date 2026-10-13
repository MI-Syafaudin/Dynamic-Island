#pragma once

#include "module_base.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <string>

namespace di {

class QuickActionModule : public ModuleBase {
public:
    QuickActionModule();
    ~QuickActionModule() override;

    void update() override;
    void update_async();
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;
    void get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h) override;

    bool is_dark_active() const { return m_dark_active.load(); }
    bool is_night_active() const { return m_night_active.load(); }
    bool is_eye_active() const { return m_night_active.load(); }
    bool is_wifi_active() const { return m_wifi_active.load(); }
    bool is_bt_active() const { return m_bt_active.load(); }
    bool is_mute_active() const { return m_mute_active.load(); }

    void set_dark_mode(bool enable);

private:
    std::atomic<bool> m_wifi_active{false};
    std::atomic<bool> m_bt_active{false};
    std::atomic<bool> m_mute_active{false};
    std::atomic<bool> m_dark_active{false};
    std::atomic<bool> m_night_active{false};

    std::atomic<bool> m_updating{false};
    std::thread m_worker_thread;
    std::mutex m_worker_mutex;

    void query_states();
};

} // namespace di
