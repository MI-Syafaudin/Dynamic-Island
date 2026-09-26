#pragma once

#include <memory>
#include <vector>
#include <chrono>
#include "config.hpp"
#include "wayland.hpp"
#include "renderer.hpp"
#include "hyprland_ipc.hpp"
#include "ipc_server.hpp"
#include "modules/clock_module.hpp"
#include "modules/workspace_module.hpp"
#include "modules/window_module.hpp"
#include "modules/audio_module.hpp"
#include "modules/media_module.hpp"
#include "modules/battery_module.hpp"
#include "modules/network_module.hpp"
#include "modules/system_module.hpp"
#include "modules/screenshot_module.hpp"
#include "modules/notification_module.hpp"
#include "modules/quickaction_module.hpp"

namespace di {

class App {
public:
    App();
    ~App();

    bool init(const std::string& config_path = "");
    void run();
    void stop();

    void expand_to(IslandMode mode, int timeout_ms = 0);
    void collapse();
    void toggle_mode(IslandMode mode);

    void set_brightness(int delta_percent);
    void notify(const std::string& app_name, const std::string& message);
    void capture_screenshot(bool area_select);

private:
    bool m_running = false;
    Config m_config;
    WaylandBackend m_wayland;
    Renderer m_renderer;
    HyprlandIpc m_hyprland_ipc;
    IpcServer m_ipc_server;

    // Modules
    std::shared_ptr<ClockModule> m_clock_mod;
    std::shared_ptr<WorkspaceModule> m_ws_mod;
    std::shared_ptr<WindowModule> m_win_mod;
    std::shared_ptr<AudioModule> m_audio_mod;
    std::shared_ptr<MediaModule> m_media_mod;
    std::shared_ptr<BatteryModule> m_bat_mod;
    std::shared_ptr<NetworkModule> m_net_mod;
    std::shared_ptr<SystemModule> m_sys_mod;
    std::shared_ptr<ScreenshotModule> m_shot_mod;
    std::shared_ptr<NotificationModule> m_notif_mod;
    std::shared_ptr<QuickActionModule> m_quick_mod;

    std::vector<std::shared_ptr<ModuleBase>> m_modules;
    std::vector<HitBox> m_current_hitboxes;

    // Animation & State
    IslandMode m_mode = IslandMode::Idle;
    double m_curr_w = 260.0;
    double m_curr_h = 34.0;
    double m_start_w = 260.0;
    double m_start_h = 34.0;
    double m_target_w = 260.0;
    double m_target_h = 34.0;

    bool m_animating = false;
    std::chrono::steady_clock::time_point m_anim_start;
    std::chrono::steady_clock::time_point m_auto_collapse_time;
    bool m_has_auto_collapse = false;

    // Periodic timers
    std::chrono::steady_clock::time_point m_last_clock_update;
    std::chrono::steady_clock::time_point m_last_system_update;
    std::chrono::steady_clock::time_point m_last_media_update;
    std::chrono::steady_clock::time_point m_last_slow_update;

    int m_brightness_val = 65;
    bool m_is_fullscreen = false;

    void setup_callbacks();
    void handle_frame();
    void render_current_state();

    void on_mouse_button(double x, double y, uint32_t button, bool pressed);
    void on_mouse_motion(double x, double y);
    void on_mouse_scroll(double delta);

    std::string handle_ipc_command(const std::string& cmd, const std::vector<std::string>& args);
    void get_target_dimensions(IslandMode mode, double& w, double& h);
    double calculate_idle_width();
    void update_idle_dimensions(bool animate = true);
    void query_brightness();
};

} // namespace di
