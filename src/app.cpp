#include "app.hpp"
#include <sys/poll.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace di {

static void run_brightnessctl_async(const std::string& val_str) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("brightnessctl", "brightnessctl", "set", val_str.c_str(), "-q", (char*)nullptr);
        _exit(1);
    }
}

App::App() {}

App::~App() {
    stop();
}

void App::play_alarm_sound() {
    stop_alarm_sound();
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        if (!m_config.sound_alarm.empty() && !m_config.audio_player.empty()) {
            execlp(m_config.audio_player.c_str(), m_config.audio_player.c_str(), m_config.sound_alarm.c_str(), (char*)nullptr);
        }
        // Terminal bell fallback
        printf("\a");
        _exit(0);
    } else if (pid > 0) {
        m_sound_pid = pid;
    }
}

void App::play_ting_sound() {
    stop_alarm_sound();
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        if (!m_config.sound_ting.empty() && !m_config.audio_player.empty()) {
            execlp(m_config.audio_player.c_str(), m_config.audio_player.c_str(), m_config.sound_ting.c_str(), (char*)nullptr);
        }
        // Terminal bell fallback
        printf("\a");
        _exit(0);
    } else if (pid > 0) {
        m_sound_pid = pid;
    }
}

void App::stop_alarm_sound() {
    if (m_sound_pid > 0) {
        kill(m_sound_pid, SIGTERM);
        m_sound_pid = -1;
    }
}

void App::dismiss_alarm() {
    stop_alarm_sound();
    if (m_clock_mod) {
        m_clock_mod->dismiss_alarm();
    }
    m_idle_width_dirty = true;
    collapse();
}

void App::snooze_alarm(int minutes) {
    stop_alarm_sound();
    if (m_clock_mod) {
        m_clock_mod->snooze_alarm(minutes);
    }
    m_idle_width_dirty = true;
    collapse();
}

void App::dismiss_battery_alarm() {
    stop_alarm_sound();
    if (m_bat_mod) {
        m_bat_mod->dismiss_alarm();
    }
    m_idle_width_dirty = true;
    collapse();
}

void App::snooze_battery_alarm(int minutes) {
    stop_alarm_sound();
    if (m_bat_mod) {
        m_bat_mod->snooze_alarm(minutes);
    }
    m_idle_width_dirty = true;
    collapse();
}

void App::trigger_charging_notification() {
    if (!m_bat_mod) return;

    int pct = m_bat_mod->get_percentage();
    std::string title = (pct >= 95) ? "Baterai Terisi Penuh" : "Pengisian Daya Aktif";
    std::string msg = "Pengisi daya terhubung. Baterai: " + std::to_string(pct) + "%";

    // 1. Non-blocking desktop notification via notify-send
    pid_t b_pid = fork();
    if (b_pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("notify-send", "notify-send", "-u", "normal", "-i", "battery-charging",
               title.c_str(), msg.c_str(), (char*)nullptr);
        _exit(0);
    }

    // 2. Dismiss any low-battery alarm state
    if (m_bat_mod->is_alarm_active()) {
        m_bat_mod->dismiss_alarm();
    }

    // 3. Dynamic Island charging banner with chime (3500ms timeout)
    if (m_mode != IslandMode::Expanded_Alarm) {
        expand_to(IslandMode::Expanded_BatteryAlert, 3500);
        play_ting_sound();
    }
}

bool App::init(const std::string& config_path) {
    if (!config_path.empty()) {
        m_config.load_from_file(config_path);
    } else {
        std::string def_path = Config::get_default_config_path();
        if (!m_config.load_from_file(def_path)) {
            m_config.load_default();
        }
    }

    m_curr_w = m_config.idle_width;
    m_curr_h = m_config.idle_height;
    m_target_w = m_curr_w;
    m_target_h = m_curr_h;

    // Initialize modules
    if (m_config.modules.clock) m_clock_mod = std::make_shared<ClockModule>();
    if (m_config.modules.workspace) m_ws_mod = std::make_shared<WorkspaceModule>();
    if (m_config.modules.active_window) m_win_mod = std::make_shared<WindowModule>(&m_config);
    if (m_config.modules.audio) m_audio_mod = std::make_shared<AudioModule>();
    if (m_config.modules.media) m_media_mod = std::make_shared<MediaModule>();
    if (m_config.modules.battery) m_bat_mod = std::make_shared<BatteryModule>();
    if (m_config.modules.network) {
        m_net_mod = std::make_shared<NetworkModule>();
        m_net_mod->set_ping_target(m_config.monitor.ping_target.empty() ? m_config.ping_target : m_config.monitor.ping_target);
        m_net_mod->set_ping_label(m_config.monitor.ping_label);
        m_net_mod->set_expanded_interval(m_config.monitor.network_expanded_interval_ms);
    }
    if (m_config.modules.system) {
        m_sys_mod = std::make_shared<SystemModule>();
        m_sys_mod->configure(
            m_config.monitor.cpu_smoothing_alpha,
            m_config.monitor.gpu_smoothing_alpha,
            m_config.monitor.gpu_name,
            m_config.monitor.thermal_path,
            m_config.monitor.disk_path
        );
    }
    if (m_config.modules.screenshot) m_shot_mod = std::make_shared<ScreenshotModule>();
    if (m_config.modules.notification) m_notif_mod = std::make_shared<NotificationModule>();
    if (m_config.modules.quickaction) m_quick_mod = std::make_shared<QuickActionModule>(&m_config);

    m_modules = {
        m_clock_mod, m_ws_mod, m_win_mod, m_audio_mod, m_media_mod,
        m_bat_mod, m_net_mod, m_sys_mod, m_shot_mod, m_notif_mod, m_quick_mod
    };

    m_renderer.init(m_config);

    double initial_w = calculate_idle_width();
    m_curr_w = initial_w;
    m_target_w = initial_w;
    m_curr_h = m_config.idle_height;
    m_target_h = m_config.idle_height;

    if (!m_wayland.init(static_cast<int>(m_curr_w), static_cast<int>(m_curr_h), m_config.margin_top, m_config.layer)) {
        return false;
    }

    m_ipc_server.start();

    // Detect sysfs backlight device for instant, zero-fork brightness reading
    m_backlight_path.clear();
    m_backlight_max = 0;
    if (std::filesystem::exists("/sys/class/backlight")) {
        for (const auto& entry : std::filesystem::directory_iterator("/sys/class/backlight")) {
            std::string max_p = (entry.path() / "max_brightness").string();
            std::string cur_p = (entry.path() / "brightness").string();
            if (std::filesystem::exists(max_p) && std::filesystem::exists(cur_p)) {
                std::ifstream mf(max_p);
                int max_val = 0;
                if (mf >> max_val && max_val > 0) {
                    m_backlight_path = entry.path().string();
                    m_backlight_max = max_val;
                    break;
                }
            }
        }
    }

    setup_callbacks();
    query_brightness();

    auto now = std::chrono::steady_clock::now();
    m_last_clock_update = now;
    m_last_system_update = now;
    m_last_media_update = now;
    m_last_slow_update = now;
    m_last_network_update = now;

    return true;
}

void App::query_brightness() {
    if (m_backlight_path.empty() || m_backlight_max <= 0) {
        if (std::filesystem::exists("/sys/class/backlight")) {
            for (const auto& entry : std::filesystem::directory_iterator("/sys/class/backlight")) {
                std::string max_p = (entry.path() / "max_brightness").string();
                std::string cur_p = (entry.path() / "brightness").string();
                if (std::filesystem::exists(max_p) && std::filesystem::exists(cur_p)) {
                    std::ifstream mf(max_p);
                    int max_val = 0;
                    if (mf >> max_val && max_val > 0) {
                        m_backlight_path = entry.path().string();
                        m_backlight_max = max_val;
                        break;
                    }
                }
            }
        }
    }

    if (!m_backlight_path.empty() && m_backlight_max > 0) {
        std::string cur_p = m_backlight_path + "/brightness";
        std::ifstream bf(cur_p);
        int cur_val = 0;
        if (bf >> cur_val) {
            m_brightness_val = std::clamp(static_cast<int>(std::round((cur_val * 100.0) / m_backlight_max)), 0, 100);
            return;
        }
    }

    FILE* fp = popen("brightnessctl -m 2>/dev/null", "r");
    if (!fp) return;

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        // format: device,class,curr,max,percent%
        std::string line = buffer;
        size_t pct_pos = line.find('%');
        if (pct_pos != std::string::npos) {
            size_t comma = line.rfind(',', pct_pos);
            if (comma != std::string::npos) {
                m_brightness_val = std::atoi(line.substr(comma + 1, pct_pos - comma - 1).c_str());
            }
        }
    }
    pclose(fp);
}

void App::set_brightness(int delta_percent) {
    m_brightness_val = std::clamp(m_brightness_val + delta_percent, 1, 100);
    std::string val_str = (delta_percent >= 0)
        ? std::to_string(delta_percent) + "%+"
        : std::to_string(-delta_percent) + "%-";
    run_brightnessctl_async(val_str);
    expand_to(IslandMode::Expanded_Brightness, m_config.timeouts.brightness_ms);
}

void App::setup_callbacks() {
    // Mouse callbacks
    m_wayland.set_mouse_button_callback([this](double x, double y, uint32_t btn, bool pr) {
        on_mouse_button(x, y, btn, pr);
    });

    m_wayland.set_mouse_motion_callback([this](double x, double y) {
        on_mouse_motion(x, y);
    });

    m_wayland.set_mouse_scroll_callback([this](double delta) {
        on_mouse_scroll(delta);
    });

    m_wayland.set_frame_callback([this]() {
        handle_frame();
    });

    // Hyprland IPC callbacks
    m_hyprland_ipc.set_workspace_callback([this](int ws) {
        if (m_ws_mod) {
            m_ws_mod->set_active_workspace(ws);
            m_idle_width_dirty = true;
            update_idle_dimensions(false);
        }
    });

    m_hyprland_ipc.set_window_callback([this](const std::string& cls, const std::string& title) {
        if (m_win_mod) {
            m_win_mod->set_active_window(cls, title);
            m_idle_width_dirty = true;
            update_idle_dimensions(false);
        }
    });

    m_hyprland_ipc.set_fullscreen_callback([this](bool fs) {
        m_is_fullscreen = fs;
        if (m_config.hide_on_fullscreen) {
            m_wayland.set_visible(!fs);
        }
    });

    // CLI IPC Server callback
    m_ipc_server.set_command_callback([this](const std::string& cmd, const std::vector<std::string>& args) {
        return handle_ipc_command(cmd, args);
    });
}

double App::calculate_idle_width() {
    if (!m_config.adaptive_width) {
        return m_config.idle_width;
    }
    if (!m_idle_width_dirty) {
        return m_cached_idle_width;
    }
    int content_w = m_renderer.calculate_compact_width(m_config, m_modules);
    double target = std::max(static_cast<double>(m_config.idle_width), static_cast<double>(content_w));
    if (m_config.max_idle_width > 0) {
        target = std::min(target, static_cast<double>(m_config.max_idle_width));
    }
    m_cached_idle_width = target;
    m_idle_width_dirty = false;
    return target;
}

void App::update_idle_dimensions(bool animate) {
    if (m_mode != IslandMode::Idle) return;

    double needed_w = calculate_idle_width();
    if (std::abs(needed_w - m_target_w) >= 1.0) {
        if (animate) {
            m_start_w = m_curr_w;
            m_start_h = m_curr_h;
            m_target_w = needed_w;
            m_target_h = m_config.idle_height;
            m_animating = true;
            m_anim_start = std::chrono::steady_clock::now();
            m_wayland.request_frame_callback();
            return;
        } else {
            m_animating = false;
            m_curr_w = needed_w;
            m_target_w = needed_w;
            m_curr_h = m_config.idle_height;
            m_target_h = m_config.idle_height;
            m_wayland.resize(static_cast<int>(m_curr_w), static_cast<int>(m_curr_h));
        }
    }
    render_current_state();
}

void App::get_target_dimensions(IslandMode mode, double& w, double& h) {
    switch (mode) {
        case IslandMode::Idle:
            w = calculate_idle_width();
            h = m_config.idle_height;
            break;
        case IslandMode::Expanded_Audio:
        case IslandMode::Expanded_Brightness:
            w = 290.0;
            h = 48.0;
            break;
        case IslandMode::Expanded_Media:
            w = 380.0;
            h = 124.0;
            break;
        case IslandMode::Expanded_System:
            if (m_sys_mod) {
                m_sys_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 340.0;
                h = 168.0;
            }
            break;
        case IslandMode::Expanded_Clock:
            w = 380.0;
            h = 204.0;
            break;
        case IslandMode::Expanded_Alarm:
            w = 360.0;
            h = 96.0;
            break;
        case IslandMode::Expanded_BatteryAlert:
            if (m_bat_mod) {
                m_bat_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 420.0;
                h = 64.0;
            }
            break;
        case IslandMode::Expanded_Network:
            if (m_net_mod) {
                m_net_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 380.0;
                h = 170.0;
            }
            break;
        case IslandMode::Expanded_Notification:
            if (m_notif_mod) {
                m_notif_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 360.0;
                h = 66.0;
            }
            break;
        case IslandMode::Expanded_Screenshot:
            if (m_shot_mod) {
                m_shot_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 360.0;
                h = 64.0;
            }
            break;
        case IslandMode::Expanded_QuickActions:
            if (m_quick_mod) {
                m_quick_mod->get_preferred_dimensions(m_renderer.get_font_desc(), m_config, w, h, m_renderer.get_measure_cr());
            } else {
                w = 540.0;
                h = 76.0;
            }
            break;
        default:
            w = m_config.idle_width;
            h = m_config.idle_height;
            break;
    }
}

void App::expand_to(IslandMode mode, int timeout_ms) {
    if (m_mode == mode) {
        if (timeout_ms > 0) {
            m_has_auto_collapse = true;
            m_auto_collapse_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        }
        double new_target_w = m_target_w;
        double new_target_h = m_target_h;
        get_target_dimensions(m_mode, new_target_w, new_target_h);
        if (std::abs(new_target_w - m_curr_w) > 1.0 || std::abs(new_target_h - m_curr_h) > 1.0) {
            m_start_w = m_curr_w;
            m_start_h = m_curr_h;
            m_target_w = new_target_w;
            m_target_h = new_target_h;
            m_animating = true;
            m_anim_start = std::chrono::steady_clock::now();
            m_wayland.request_frame_callback();
            return;
        }
        if (!m_animating) {
            render_current_state();
        }
        return;
    }

    m_mode = mode;
    m_start_w = m_curr_w;
    m_start_h = m_curr_h;
    get_target_dimensions(m_mode, m_target_w, m_target_h);

    m_animating = true;
    m_anim_start = std::chrono::steady_clock::now();

    if (mode == IslandMode::Expanded_System && m_sys_mod) {
        m_sys_mod->update(true);
        m_last_system_update = m_anim_start;
    }
    if (mode == IslandMode::Expanded_Media && m_media_mod) {
        m_media_mod->update();
        m_media_mod->on_expanded();
        m_last_media_update = m_anim_start;
    }
    if (mode == IslandMode::Expanded_Audio && m_audio_mod) {
        m_audio_mod->update();
    }
    if (mode == IslandMode::Expanded_Brightness) {
        query_brightness();
    }
    if (mode == IslandMode::Expanded_QuickActions && m_quick_mod) {
        m_quick_mod->update_async();
    }
    if (mode == IslandMode::Expanded_Network && m_net_mod) {
        m_net_mod->on_expanded();
    }

    if (timeout_ms > 0) {
        m_has_auto_collapse = true;
        m_auto_collapse_time = m_anim_start + std::chrono::milliseconds(timeout_ms);
    } else {
        m_has_auto_collapse = false;
    }

    m_wayland.request_frame_callback();
}

void App::collapse() {
    if (m_mode == IslandMode::Idle && !m_animating) return;

    if (m_net_mod) {
        m_net_mod->on_collapsed();
    }

    if (m_mode == IslandMode::Expanded_BatteryAlert && m_bat_mod) {
        if (!m_bat_mod->is_alarm_active()) {
            m_bat_mod->dismiss_alarm();
        }
    }

    m_mode = IslandMode::Idle;
    m_has_auto_collapse = false;

    m_start_w = m_curr_w;
    m_start_h = m_curr_h;
    m_idle_width_dirty = true;
    get_target_dimensions(IslandMode::Idle, m_target_w, m_target_h);

    m_animating = true;
    m_anim_start = std::chrono::steady_clock::now();

    m_wayland.request_frame_callback();
}

void App::toggle_mode(IslandMode mode) {
    if (m_mode == mode) {
        collapse();
    } else {
        expand_to(mode, 0);
    }
}

void App::notify(const std::string& app_name, const std::string& message) {
    if (m_notif_mod) {
        m_notif_mod->post_notification(app_name, message);
    }
    expand_to(IslandMode::Expanded_Notification, m_config.timeouts.notification_ms);
}

void App::capture_screenshot(bool area_select) {
    if (m_shot_mod) {
        m_shot_mod->take_screenshot(area_select);
    }
    expand_to(IslandMode::Expanded_Screenshot, m_config.timeouts.screenshot_ms);
}

void App::handle_frame() {
    if (m_animating) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(now - m_anim_start).count();
        double t = elapsed_ms / m_config.animation_duration_ms;

        if (t >= 1.0) {
            t = 1.0;
            m_curr_w = m_target_w;
            m_curr_h = m_target_h;
            m_animating = false;
        } else {
            // Cubic ease-out
            double ease = 1.0 - std::pow(1.0 - t, 3.0);
            m_curr_w = m_start_w + (m_target_w - m_start_w) * ease;
            m_curr_h = m_start_h + (m_target_h - m_start_h) * ease;
            m_wayland.request_frame_callback();
        }

        m_wayland.resize(static_cast<int>(m_curr_w), static_cast<int>(m_curr_h));
    }

    render_current_state();
}

void App::render_current_state() {
    cairo_t* cr = m_wayland.begin_frame();
    if (!cr) return;

    double cr_radius = (m_mode == IslandMode::Idle)
        ? (m_curr_h / 2.0)
        : std::min(static_cast<double>(m_config.corner_radius + 4), m_curr_h / 2.0);

    m_renderer.render_island(
        cr,
        static_cast<int>(m_curr_w),
        static_cast<int>(m_curr_h),
        cr_radius,
        m_mode,
        m_config,
        m_modules,
        m_current_hitboxes,
        m_brightness_val
    );

    m_wayland.end_frame();
}

void App::on_mouse_button(double x, double y, uint32_t button, bool pressed) {
    if (!pressed) return;

    if (button == BTN_RIGHT) {
        toggle_mode(IslandMode::Expanded_QuickActions);
        return;
    }

    if (button == BTN_LEFT) {
        // Check hitboxes
        for (const auto& hb : m_current_hitboxes) {
            if (x >= hb.x && x <= hb.x + hb.width &&
                y >= hb.y && y <= hb.y + hb.height) {

                if (hb.action == "collapse") {
                    collapse();
                    return;
                }
                if (hb.action == "audio_set_volume") {
                    double pct = std::clamp((x - 20.0) / (m_curr_w - 40.0), 0.0, 1.0);
                    int vol = static_cast<int>(std::round(pct * 100.0));
                    if (m_audio_mod) m_audio_mod->set_volume(vol);
                    render_current_state();
                    return;
                }
                if (hb.action == "brightness_set") {
                    double pct = std::clamp((x - 20.0) / (m_curr_w - 40.0), 0.05, 1.0);
                    int br = static_cast<int>(std::round(pct * 100.0));
                    m_brightness_val = br;
                    run_brightnessctl_async(std::to_string(br) + "%");
                    render_current_state();
                    return;
                }
                if (hb.action == "alarm_dismiss") {
                    dismiss_alarm();
                    return;
                }
                if (hb.action == "alarm_snooze") {
                    snooze_alarm(5);
                    return;
                }
                if (hb.action == "battery_alarm_dismiss" || hb.action == "battery_dismiss") {
                    dismiss_battery_alarm();
                    return;
                }
                if (hb.action == "battery_alarm_snooze" || hb.action == "battery_snooze") {
                    snooze_battery_alarm(5);
                    return;
                }
                if (hb.action == "battery_info") {
                    toggle_mode(IslandMode::Expanded_BatteryAlert);
                    return;
                }
                if (hb.action == "alarm_cancel") {
                    if (m_clock_mod) {
                        int id = std::atoi(hb.param.c_str());
                        m_clock_mod->cancel_alarm(id);
                    }
                    m_idle_width_dirty = true;
                    render_current_state();
                    return;
                }
                if (hb.action == "quick_timer") {
                    if (m_clock_mod) {
                        int mins = std::atoi(hb.param.c_str());
                        std::string lbl = (mins >= 60) ? ("Timer " + std::to_string(mins / 60) + "h") : ("Timer " + hb.param + "m");
                        m_clock_mod->add_timer(mins * 60, lbl);
                    }
                    m_idle_width_dirty = true;
                    if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                    else render_current_state();
                    return;
                }
                if (hb.action == "time_adj" || hb.action == "set_custom_alarm" || hb.action == "set_custom_timer") {
                    if (m_clock_mod) {
                        m_clock_mod->handle_click(hb.action, hb.param);
                    }
                    m_idle_width_dirty = true;
                    if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                    else render_current_state();
                    return;
                }
                if (hb.action == "toggle_expand") {
                    if (hb.param == "clock") toggle_mode(IslandMode::Expanded_Clock);
                    else if (hb.param == "alarm") toggle_mode(IslandMode::Expanded_Alarm);
                    else if (hb.param == "battery" || hb.param == "battery_alert") toggle_mode(IslandMode::Expanded_BatteryAlert);
                    else if (hb.param == "media") toggle_mode(IslandMode::Expanded_Media);
                    else if (hb.param == "system") toggle_mode(IslandMode::Expanded_System);
                    else if (hb.param == "network") toggle_mode(IslandMode::Expanded_Network);
                    else if (hb.param == "notification") toggle_mode(IslandMode::Expanded_Notification);
                    else if (hb.param == "quick") toggle_mode(IslandMode::Expanded_QuickActions);
                    return;
                }
                if (hb.action == "qa_shot") {
                    capture_screenshot(true);
                    return;
                }

                // Check modules for handler
                for (auto& mod : m_modules) {
                    if (mod && mod->handle_click(hb.action, hb.param)) {
                        if (hb.action == "qa_mute" && m_audio_mod) {
                            m_audio_mod->update();
                        }
                        render_current_state();
                        return;
                    }
                }
            }
        }

        // If clicked on island body but no specific button, toggle clock/idle
        if (m_mode == IslandMode::Expanded_Alarm) {
            dismiss_alarm();
        } else if (m_mode == IslandMode::Expanded_BatteryAlert) {
            dismiss_battery_alarm();
        } else if (m_mode != IslandMode::Idle) {
            collapse();
        }
    }
}

void App::on_mouse_motion(double x, double y) {
    (void)x;
    (void)y;
}

void App::on_mouse_scroll(double delta) {
    if (m_mode == IslandMode::Expanded_Brightness) {
        int step = (delta > 0) ? -5 : 5;
        set_brightness(step);
    } else if (m_mode == IslandMode::Expanded_Clock) {
        if (m_clock_mod) {
            m_clock_mod->handle_scroll(m_wayland.get_pointer_x(), m_wayland.get_pointer_y(), delta);
            render_current_state();
        }
    } else if (m_audio_mod) {
        int step = (delta > 0) ? -5 : 5;
        m_audio_mod->change_volume(step);
        expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
    }
}

std::string App::handle_ipc_command(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "toggle") {
        if (!args.empty()) {
            std::string target = args[0];
            if (target == "audio") toggle_mode(IslandMode::Expanded_Audio);
            else if (target == "brightness") toggle_mode(IslandMode::Expanded_Brightness);
            else if (target == "media") toggle_mode(IslandMode::Expanded_Media);
            else if (target == "system") toggle_mode(IslandMode::Expanded_System);
            else if (target == "clock") toggle_mode(IslandMode::Expanded_Clock);
            else if (target == "alarm") toggle_mode(IslandMode::Expanded_Alarm);
            else if (target == "battery" || target == "battery_alert") toggle_mode(IslandMode::Expanded_BatteryAlert);
            else if (target == "network") toggle_mode(IslandMode::Expanded_Network);
            else if (target == "quick") toggle_mode(IslandMode::Expanded_QuickActions);
            return "OK";
        }
        if (m_mode == IslandMode::Idle) {
            expand_to(IslandMode::Expanded_QuickActions, 0);
        } else {
            collapse();
        }
        return "OK";
    }
    if (cmd == "collapse") {
        collapse();
        return "OK";
    }
    if (cmd == "expand") {
        if (!args.empty()) {
            std::string target = args[0];
            if (target == "audio") {
                if (m_mode == IslandMode::Expanded_Audio) {
                    collapse();
                } else {
                    if (m_audio_mod) m_audio_mod->update();
                    expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
                }
            } else if (target == "brightness") {
                if (m_mode == IslandMode::Expanded_Brightness) {
                    collapse();
                } else {
                    query_brightness();
                    expand_to(IslandMode::Expanded_Brightness, m_config.timeouts.brightness_ms);
                }
            } else if (target == "media") {
                if (m_mode == IslandMode::Expanded_Media) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_Media, 0);
                }
            } else if (target == "system") {
                if (m_mode == IslandMode::Expanded_System) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_System, 0);
                }
            } else if (target == "clock") {
                if (m_mode == IslandMode::Expanded_Clock) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_Clock, 0);
                }
            } else if (target == "alarm") {
                if (m_mode == IslandMode::Expanded_Alarm) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_Alarm, 0);
                }
            } else if (target == "battery" || target == "battery_alert") {
                if (m_mode == IslandMode::Expanded_BatteryAlert) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_BatteryAlert, 0);
                }
            } else if (target == "network") {
                if (m_mode == IslandMode::Expanded_Network) {
                    collapse();
                } else {
                    if (m_net_mod) m_net_mod->update();
                    expand_to(IslandMode::Expanded_Network, 0);
                }
            } else if (target == "quick") {
                if (m_mode == IslandMode::Expanded_QuickActions) {
                    collapse();
                } else {
                    expand_to(IslandMode::Expanded_QuickActions, 0);
                }
            }
            return "OK";
        }
    }
    if (cmd == "volume") {
        if (!args.empty() && m_audio_mod) {
            std::string val = args[0];
            if (val == "toggle" || val == "mute") {
                m_audio_mod->toggle_mute();
            } else {
                int delta = 5;
                if (sscanf(val.c_str(), "%d", &delta) == 1) {
                    m_audio_mod->change_volume(delta);
                } else if (val.find('+') != std::string::npos) {
                    m_audio_mod->change_volume(5);
                } else if (val.find('-') != std::string::npos) {
                    m_audio_mod->change_volume(-5);
                }
            }
            expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
            return "OK";
        }
    }
    if (cmd == "brightness") {
        if (!args.empty()) {
            std::string val = args[0];
            int delta = 5;
            if (sscanf(val.c_str(), "%d", &delta) == 1) {
                set_brightness(delta);
            } else if (val.find('+') != std::string::npos) {
                set_brightness(5);
            } else if (val.find('-') != std::string::npos) {
                set_brightness(-5);
            }
            return "OK";
        }
    }
    if (cmd == "media") {
        if (!args.empty() && m_media_mod) {
            std::string act = args[0];
            if (act == "play-pause" || act == "toggle") m_media_mod->play_pause();
            else if (act == "next") m_media_mod->next_track();
            else if (act == "previous" || act == "prev") m_media_mod->prev_track();
            expand_to(IslandMode::Expanded_Media, 3000);
            return "OK";
        }
    }
    if (cmd == "screenshot") {
        bool area = (!args.empty() && args[0] == "area");
        capture_screenshot(area);
        return "OK";
    }
    if (cmd == "notify") {
        std::string app_name = args.size() > 0 ? args[0] : "Notification";
        std::string msg;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) msg += " ";
            msg += args[i];
        }
        notify(app_name, msg);
        return "OK";
    }
    if (cmd == "battery") {
        std::string sub = (args.size() > 0) ? args[0] : "";
        std::string param = (args.size() > 1) ? args[1] : "";

        if (sub == "dismiss") {
            dismiss_battery_alarm();
            return "Battery alert dismissed\n";
        }
        if (sub == "snooze") {
            int mins = 5;
            if (!param.empty()) mins = std::atoi(param.c_str());
            if (mins <= 0) mins = 5;
            snooze_battery_alarm(mins);
            return "Battery alert snoozed for " + std::to_string(mins) + " minutes\n";
        }
        if (sub == "alert" || sub == "test") {
            int test_lvl = 5;
            if (!param.empty()) test_lvl = std::atoi(param.c_str());
            if (test_lvl <= 0) test_lvl = 5;
            if (m_bat_mod) {
                m_bat_mod->trigger_test_alert(test_lvl);
                expand_to(IslandMode::Expanded_BatteryAlert, 6000);
                play_ting_sound();
                return "Battery alert triggered (test mode: " + std::to_string(test_lvl) + "%)\n";
            }
            return "Battery module not available\n";
        }
        if (sub == "charging" || sub == "test-charging") {
            trigger_charging_notification();
            return "Battery charging notification triggered\n";
        }

        if (!m_bat_mod || !m_bat_mod->is_available()) {
            return "Battery not available\n";
        }
        std::string status = m_bat_mod->is_charging() ? "Charging" : "Discharging";
        return "Battery: " + std::to_string(m_bat_mod->get_percentage()) + "% (" + status + ")\n";
    }
    if (cmd == "alarm") {
        if (args.empty() || args[0] == "list") {
            if (m_clock_mod) {
                return m_clock_mod->get_alarms_list_str();
            }
            return "Clock module not available\n";
        }
        std::string sub = args[0];

        // Check if sub is a direct time string like "07:30", "7:30", "07.30"
        bool is_direct_time = false;
        size_t sep_pos = sub.find(':');
        if (sep_pos == std::string::npos) {
            sep_pos = sub.find('.');
        }
        if (sep_pos != std::string::npos && sep_pos > 0 && sep_pos < sub.size() - 1) {
            bool all_digits = true;
            for (size_t i = 0; i < sub.size(); ++i) {
                if (i != sep_pos && !std::isdigit(static_cast<unsigned char>(sub[i]))) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) is_direct_time = true;
        }

        // Check if sub is a direct duration string like "10m", "300s", "1h", or a number "15"
        bool is_direct_duration = false;
        if (!is_direct_time && sub != "set" && sub != "in" && sub != "timer" &&
            sub != "cancel" && sub != "dismiss" && sub != "stop" && sub != "snooze") {
            bool valid_dur = true;
            int digit_count = 0;
            for (char c : sub) {
                if (std::isdigit(static_cast<unsigned char>(c))) digit_count++;
                else if (c != 'm' && c != 'M' && c != 's' && c != 'S' && c != 'h' && c != 'H') {
                    valid_dur = false;
                    break;
                }
            }
            if (valid_dur && digit_count > 0) is_direct_duration = true;
        }

        if (is_direct_time || sub == "set") {
            std::string time_str = is_direct_time ? sub : (args.size() > 1 ? args[1] : "");
            if (time_str.empty()) return "Usage: alarm set <HH:MM> [label]\n";
            size_t colon = time_str.find(':');
            if (colon == std::string::npos) colon = time_str.find('.');
            if (colon == std::string::npos) return "Invalid time format. Use HH:MM\n";
            int h = std::atoi(time_str.substr(0, colon).c_str());
            int m = std::atoi(time_str.substr(colon + 1).c_str());
            if (h < 0 || h > 23 || m < 0 || m > 59) return "Invalid time: " + time_str + "\n";
            size_t label_start = is_direct_time ? 1 : 2;
            std::string label;
            for (size_t i = label_start; i < args.size(); ++i) {
                if (i > label_start) label += " ";
                label += args[i];
            }
            if (m_clock_mod) {
                int id = m_clock_mod->add_alarm(h, m, label);
                m_idle_width_dirty = true;
                if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                char time_formatted[16];
                snprintf(time_formatted, sizeof(time_formatted), "%02d:%02d", h, m);
                return "Alarm set for " + std::string(time_formatted) + " (\"" + (label.empty() ? "Alarm" : label) + "\") [ID: " + std::to_string(id) + "]\n";
            }
            return "Clock module not available\n";
        }
        if (is_direct_duration || sub == "in" || sub == "timer") {
            std::string dur_str = is_direct_duration ? sub : (args.size() > 1 ? args[1] : "");
            if (dur_str.empty()) return "Usage: alarm in <duration[s|m|h]> [label]\n";
            int total_secs = 0;
            if (dur_str.back() == 's' || dur_str.back() == 'S') {
                total_secs = std::atoi(dur_str.substr(0, dur_str.size() - 1).c_str());
            } else if (dur_str.back() == 'm' || dur_str.back() == 'M') {
                total_secs = std::atoi(dur_str.substr(0, dur_str.size() - 1).c_str()) * 60;
            } else if (dur_str.back() == 'h' || dur_str.back() == 'H') {
                total_secs = std::atoi(dur_str.substr(0, dur_str.size() - 1).c_str()) * 3600;
            } else {
                total_secs = std::atoi(dur_str.c_str()) * 60;
            }
            if (total_secs <= 0) return "Duration must be > 0\n";
            size_t label_start = is_direct_duration ? 1 : 2;
            std::string label;
            for (size_t i = label_start; i < args.size(); ++i) {
                if (i > label_start) label += " ";
                label += args[i];
            }
            if (m_clock_mod) {
                int id = m_clock_mod->add_timer(total_secs, label);
                m_idle_width_dirty = true;
                if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                std::string dur_display;
                if (total_secs >= 3600 && total_secs % 3600 == 0) {
                    dur_display = std::to_string(total_secs / 3600) + " hour(s)";
                } else if (total_secs >= 60 && total_secs % 60 == 0) {
                    dur_display = std::to_string(total_secs / 60) + " minute(s)";
                } else {
                    dur_display = std::to_string(total_secs) + " second(s)";
                }
                return "Timer set for " + dur_display + " [ID: " + std::to_string(id) + "]\n";
            }
            return "Clock module not available\n";
        }
        if (sub == "cancel") {
            if (args.size() < 2 || args[1] == "all") {
                if (m_clock_mod) m_clock_mod->cancel_alarm(-1);
                m_idle_width_dirty = true;
                if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                return "All alarms canceled\n";
            }
            int id = std::atoi(args[1].c_str());
            if (m_clock_mod && m_clock_mod->cancel_alarm(id)) {
                m_idle_width_dirty = true;
                if (m_mode == IslandMode::Idle) update_idle_dimensions(false);
                return "Alarm " + std::to_string(id) + " canceled\n";
            }
            return "Alarm ID not found\n";
        }
        if (sub == "dismiss" || sub == "stop") {
            dismiss_alarm();
            return "Alarm dismissed\n";
        }
        if (sub == "snooze") {
            int mins = (args.size() > 1) ? std::atoi(args[1].c_str()) : 5;
            snooze_alarm(mins);
            return "Alarm snoozed for " + std::to_string(mins) + " minutes\n";
        }
        return "Unknown alarm subcommand: " + sub + "\n";
    }
    if (cmd == "timer") {
        if (args.empty()) {
            std::vector<std::string> l_args = {"list"};
            return handle_ipc_command("alarm", l_args);
        }
        std::vector<std::string> in_args = {"in", args[0]};
        for (size_t i = 1; i < args.size(); ++i) {
            in_args.push_back(args[i]);
        }
        return handle_ipc_command("alarm", in_args);
    }
    if (cmd == "dark") {
        if (m_quick_mod) {
            std::string sub = args.empty() ? "toggle" : args[0];
            if (sub == "on" || sub == "true" || sub == "1") {
                m_quick_mod->set_dark_mode(true);
            } else if (sub == "off" || sub == "false" || sub == "0") {
                m_quick_mod->set_dark_mode(false);
            } else {
                m_quick_mod->set_dark_mode(!m_quick_mod->is_dark_active());
            }
            render_current_state();
            return m_quick_mod->is_dark_active() ? "Dark mode: ON\n" : "Dark mode: OFF\n";
        }
        return "QuickAction module not available\n";
    }
    if (cmd == "night" || cmd == "eye" || cmd == "eye-comfort" || cmd == "eye_comfort") {
        if (m_quick_mod) {
            std::string sub = args.empty() ? "toggle" : args[0];
            if (sub == "on" || sub == "true" || sub == "1") {
                m_quick_mod->set_eye_comfort(true);
            } else if (sub == "off" || sub == "false" || sub == "0") {
                m_quick_mod->set_eye_comfort(false);
            } else {
                m_quick_mod->set_eye_comfort(!m_quick_mod->is_night_active());
            }
            render_current_state();
            return m_quick_mod->is_night_active() ? "Eye comfort: ON\n" : "Eye comfort: OFF\n";
        }
        return "QuickAction module not available\n";
    }
    if (cmd == "ping") {
        return "pong";
    }
    if (cmd == "version") {
        return "Dynamic Island for Hyprland v1.1\nAuthor: MI-Syafaudin\nGitHub: https://github.com/MI-Syafaudin/Dynamic-Island\n";
    }
    if (cmd == "reload") {
        m_config.load_from_file(Config::get_default_config_path());
        if (m_win_mod) m_win_mod->set_config(&m_config);
        if (m_quick_mod) m_quick_mod->set_config(&m_config);
        if (m_net_mod) {
            m_net_mod->set_ping_target(m_config.monitor.ping_target.empty() ? m_config.ping_target : m_config.monitor.ping_target);
            m_net_mod->set_ping_label(m_config.monitor.ping_label);
            m_net_mod->set_expanded_interval(m_config.monitor.network_expanded_interval_ms);
        }
        if (m_sys_mod) {
            m_sys_mod->configure(
                m_config.monitor.cpu_smoothing_alpha,
                m_config.monitor.gpu_smoothing_alpha,
                m_config.monitor.gpu_name,
                m_config.monitor.thermal_path
            );
        }
        m_renderer.update_font(m_config);
        m_wayland.set_margin_top(m_config.margin_top);
        update_idle_dimensions(false);
        return "Config reloaded";
    }
    if (cmd == "quit") {
        m_running = false;
        return "Stopping\n";
    }
    return "Unknown command: " + cmd;
}

void App::run() {
    m_running = true;
    render_current_state();

    while (m_running) {
        m_wayland.flush();

        struct pollfd fds[3];
        int nfds = 0;

        // 1. Wayland Display FD
        int wl_fd = m_wayland.get_display_fd();
        if (wl_fd >= 0) {
            fds[nfds].fd = wl_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        // 2. Hyprland IPC FD
        int hypr_fd = m_hyprland_ipc.get_fd();
        if (hypr_fd >= 0) {
            fds[nfds].fd = hypr_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        // 3. IPC Server FD
        int ipc_fd = m_ipc_server.get_fd();
        if (ipc_fd >= 0) {
            fds[nfds].fd = ipc_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        // Compute poll timeout
        int timeout_ms = 1000;
        if (m_animating) {
            timeout_ms = 16; // 60 FPS animation tick
        } else if (m_mode == IslandMode::Expanded_Media && m_media_mod && m_media_mod->needs_marquee_animation()) {
            timeout_ms = 20; // 50 FPS smooth marquee
        } else if (m_mode == IslandMode::Expanded_Network) {
            timeout_ms = std::clamp(m_config.monitor.network_expanded_interval_ms, 50, 1000); // Real-time network monitor tick
        } else if (m_mode == IslandMode::Expanded_System) {
            timeout_ms = std::clamp(m_config.monitor.system_expanded_interval_ms, 50, 1000); // Fast system monitor tick (btop-like)
        }

        m_wayland.prepare_read();
        int ret = poll(fds, nfds, timeout_ms);

        if (ret > 0) {
            bool wl_readable = false;
            for (int i = 0; i < nfds; ++i) {
                if (fds[i].fd == wl_fd) {
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        m_running = false;
                        break;
                    }
                    if (fds[i].revents & POLLIN) {
                        wl_readable = true;
                    }
                }
            }

            if (!m_running) {
                m_wayland.cancel_read();
                break;
            }

            if (wl_readable) {
                m_wayland.read_events();
            } else {
                m_wayland.cancel_read();
            }

            for (int i = 0; i < nfds; ++i) {
                if (fds[i].revents & POLLIN) {
                    if (fds[i].fd == hypr_fd) {
                        m_hyprland_ipc.process_incoming();
                    } else if (fds[i].fd == ipc_fd) {
                        m_ipc_server.process_connections();
                    }
                }
            }
        } else {
            m_wayland.cancel_read();
        }

        m_wayland.dispatch_pending();

        // If animating and poll timed out without Wayland events (e.g. empty workspace with VFR),
        // drive the frame directly so animation never hangs or stutters
        if (ret == 0 && m_animating) {
            handle_frame();
        }

        // Drive smooth marquee animation when Expanded_Media has overflowing text
        if (m_mode == IslandMode::Expanded_Media && !m_animating && m_media_mod && m_media_mod->needs_marquee_animation()) {
            static auto last_media_marquee_frame = std::chrono::steady_clock::now();
            auto marquee_now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(marquee_now - last_media_marquee_frame).count() >= 20) {
                last_media_marquee_frame = marquee_now;
                render_current_state();
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Check auto-collapse timer
        if (m_has_auto_collapse && now >= m_auto_collapse_time) {
            collapse();
        }

        // Periodic Clock update & Alarm check (every 1s)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_clock_update).count() >= 1) {
            m_last_clock_update = now;
            bool clock_dirty = false;
            static std::string last_idle_time;
            static bool last_had_timer = false;

            if (m_clock_mod) {
                m_clock_mod->update(m_mode == IslandMode::Expanded_Clock || m_mode == IslandMode::Expanded_Alarm);

                // Check alarms
                AlarmItem triggered;
                if (m_clock_mod->check_alarms(triggered)) {
                    expand_to(IslandMode::Expanded_Alarm, 0);
                    play_alarm_sound();
                    m_last_alarm_sound = now;

                    // Desktop notification via notify-send as well for maximum reliability
                    pid_t n_pid = fork();
                    if (n_pid == 0) {
                        int devnull = open("/dev/null", O_WRONLY);
                        if (devnull >= 0) {
                            dup2(devnull, STDOUT_FILENO);
                            dup2(devnull, STDERR_FILENO);
                            close(devnull);
                        }
                        std::string title = triggered.is_timer ? "Timer Selesai!" : "Alarm Berbunyi!";
                        std::string body = triggered.label.empty() ? "Waktu telah habis." : triggered.label;
                        execlp("notify-send", "notify-send", "-u", "critical", "-i", "alarm-clock", title.c_str(), body.c_str(), (char*)nullptr);
                        _exit(0);
                    }
                }

                if (m_mode == IslandMode::Expanded_Alarm) {
                    // Periodic alarm sound repeating every 3.5s while ringing
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_alarm_sound).count() >= 3500) {
                        play_alarm_sound();
                        m_last_alarm_sound = now;
                    }
                    render_current_state();
                }

                if (m_mode == IslandMode::Expanded_BatteryAlert) {
                    if (m_bat_mod) {
                        m_bat_mod->update();
                        if (m_bat_mod->is_charging() && m_bat_mod->is_alarm_active()) {
                            dismiss_battery_alarm();
                        }
                    }
                }

                bool has_timer = m_clock_mod->has_active_timer();
                if (m_clock_mod->get_time_str() != last_idle_time ||
                    has_timer ||
                    m_clock_mod->is_any_alarm_ringing()) {
                    last_idle_time = m_clock_mod->get_time_str();
                    clock_dirty = true;
                }

                if (has_timer != last_had_timer) {
                    last_had_timer = has_timer;
                    m_idle_width_dirty = true;
                }
            }

            if (m_bat_mod) {
                int old_pct = m_bat_mod->get_percentage();
                bool old_chg = m_bat_mod->is_charging();
                m_bat_mod->update();
                if (old_pct != m_bat_mod->get_percentage() || old_chg != m_bat_mod->is_charging()) {
                    m_idle_width_dirty = true;
                    if (m_mode == IslandMode::Idle) {
                        update_idle_dimensions(false);
                    } else if (m_mode == IslandMode::Expanded_BatteryAlert) {
                        render_current_state();
                    }
                }

                if (m_bat_mod->check_charging_started()) {
                    trigger_charging_notification();
                }

                // Check critical battery alert
                int crit_lvl = 0;
                std::string crit_msg;
                if (m_bat_mod->check_critical_alert(crit_lvl, crit_msg)) {
                    // Non-blocking desktop notification
                    pid_t b_pid = fork();
                    if (b_pid == 0) {
                        int devnull = open("/dev/null", O_WRONLY);
                        if (devnull >= 0) {
                            dup2(devnull, STDOUT_FILENO);
                            dup2(devnull, STDERR_FILENO);
                            close(devnull);
                        }
                        execlp("notify-send", "notify-send", "-u", "critical", "-i", "battery-caution",
                               "Peringatan Baterai", crit_msg.c_str(), (char*)nullptr);
                        _exit(0);
                    }

                    // Low & Critical battery alert: Dedicated banner with "ting" sound (6s timeout)
                    expand_to(IslandMode::Expanded_BatteryAlert, 6000);
                    play_ting_sound();
                }
            }

            if (m_mode == IslandMode::Expanded_Clock) {
                render_current_state();
            } else if (m_mode == IslandMode::Idle && clock_dirty) {
                m_idle_width_dirty = true;
                update_idle_dimensions(false);
            }
        }

        // Periodic Expanded Network update (real-time ping/jitter/loss/bandwidth)
        if (m_net_mod && m_mode == IslandMode::Expanded_Network) {
            auto elapsed_net = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_network_update).count();
            int net_interval = std::clamp(m_config.monitor.network_expanded_interval_ms, 50, 1000);
            if (m_net_mod->check_and_clear_dirty() || elapsed_net >= net_interval) {
                m_last_network_update = now;
                render_current_state();
            }
        }

        // Periodic System Monitor update (both Idle compact and Expanded_System)
        if (m_sys_mod) {
            int sys_interval = (m_mode == IslandMode::Expanded_System)
                ? std::clamp(m_config.monitor.system_expanded_interval_ms, 50, 2000)
                : std::clamp(m_config.monitor.system_idle_interval_ms, 500, 10000);

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_system_update).count() >= sys_interval) {
                m_last_system_update = now;
                int old_cpu = m_sys_mod->get_cpu_percent();
                m_sys_mod->update(m_mode == IslandMode::Expanded_System);
                int new_cpu = m_sys_mod->get_cpu_percent();

                if (m_mode == IslandMode::Expanded_System) {
                    render_current_state();
                } else if (m_mode == IslandMode::Idle && old_cpu != new_cpu) {
                    m_idle_width_dirty = true;
                    update_idle_dimensions(false);
                }
            }
        }

        // Periodic Media status update (2s when playing, 4s when idle/stopped)
        int media_interval_sec = (m_media_mod && m_media_mod->is_playing()) ? 2 : 4;
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_media_update).count() >= media_interval_sec) {
            m_last_media_update = now;
            if (m_media_mod) {
                std::string old_title = m_media_mod->get_title();
                std::string old_status = m_media_mod->get_status();
                m_media_mod->update();
                bool changed = (old_title != m_media_mod->get_title() || old_status != m_media_mod->get_status());

                if (m_mode == IslandMode::Expanded_Media) {
                    render_current_state();
                } else if (m_mode == IslandMode::Idle && changed) {
                    m_idle_width_dirty = true;
                    update_idle_dimensions(false);
                }
            }
        }

        // Periodic Slow update: Network, Audio, Quick Controls (every 5s)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_slow_update).count() >= 5) {
            m_last_slow_update = now;
            bool slow_changed = false;

            if (m_net_mod) {
                bool old_conn = m_net_mod->is_connected();
                std::string old_ssid = m_net_mod->get_ssid();
                m_net_mod->update();
                if (old_conn != m_net_mod->is_connected() || old_ssid != m_net_mod->get_ssid()) {
                    slow_changed = true;
                }
            }
            if (m_audio_mod) {
                int old_vol = m_audio_mod->get_volume();
                bool old_mute = m_audio_mod->is_muted();
                m_audio_mod->update();
                if (old_vol != m_audio_mod->get_volume() || old_mute != m_audio_mod->is_muted()) {
                    slow_changed = true;
                }
            }

            if (m_quick_mod && m_mode == IslandMode::Expanded_QuickActions) m_quick_mod->update_async();
            if (m_mode == IslandMode::Expanded_Brightness) query_brightness();

            if (m_mode == IslandMode::Idle && slow_changed) {
                m_idle_width_dirty = true;
                update_idle_dimensions(false);
            } else if (m_mode == IslandMode::Expanded_QuickActions || m_mode == IslandMode::Expanded_Audio || m_mode == IslandMode::Expanded_Brightness) {
                render_current_state();
            }
        }
    }

    stop();
}

void App::stop() {
    m_running = false;
    stop_alarm_sound();
    m_ipc_server.stop();
    m_hyprland_ipc.disconnect();
    m_wayland.cleanup();
}

} // namespace di
