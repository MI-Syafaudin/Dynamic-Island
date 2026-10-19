#pragma once

#include "module_base.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <unordered_map>

namespace di {

class QuickActionModule : public ModuleBase {
public:
    explicit QuickActionModule(const Config* config = nullptr);
    ~QuickActionModule() override;

    void set_config(const Config* config) { m_config = config; }

    void update() override;
    void update_async();
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;
    void get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr = nullptr) override;

    bool is_dark_active() const { return m_dark_active.load(); }
    bool is_night_active() const { return m_night_active.load(); }
    bool is_eye_active() const { return m_night_active.load(); }
    bool is_wifi_active() const { return m_wifi_active.load(); }
    bool is_bt_active() const { return m_bt_active.load(); }
    bool is_mute_active() const { return m_mute_active.load(); }

    std::string get_bt_icon() const {
        std::lock_guard<std::mutex> lock(m_bt_info_mutex);
        return m_bt_icon;
    }
    std::string get_bt_label() const {
        std::lock_guard<std::mutex> lock(m_bt_info_mutex);
        return m_bt_label;
    }

    void set_dark_mode(bool enable);
    void set_eye_comfort(bool enable);

private:
    const Config* m_config = nullptr;
    std::string get_shader_path() const;
    std::atomic<bool> m_wifi_active{false};
    std::atomic<bool> m_bt_active{false};
    std::atomic<bool> m_mute_active{false};
    std::atomic<bool> m_dark_active{false};
    std::atomic<bool> m_night_active{false};

    mutable std::mutex m_bt_info_mutex;
    std::string m_bt_icon{"󰂲"};
    std::string m_bt_label{"Bluetooth"};

    std::atomic<bool> m_updating{false};
    std::thread m_worker_thread;
    std::mutex m_worker_mutex;
    std::unordered_map<std::string, double> m_label_font_sizes;

    std::atomic<uint64_t> m_eye_transition_seq{0};
    std::atomic<int> m_current_shader_step{0};
    std::string m_shader_dir;

    void ensure_shaders_generated();
    std::string get_step_shader_path(int step);
    void query_states();
};

} // namespace di
