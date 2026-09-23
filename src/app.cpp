#include "app.hpp"
#include <sys/poll.h>
#include <unistd.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace di {

App::App() {}

App::~App() {
    stop();
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
    if (m_config.modules.active_window) m_win_mod = std::make_shared<WindowModule>();
    if (m_config.modules.audio) m_audio_mod = std::make_shared<AudioModule>();
    if (m_config.modules.media) m_media_mod = std::make_shared<MediaModule>();
    if (m_config.modules.battery) m_bat_mod = std::make_shared<BatteryModule>();
    if (m_config.modules.network) m_net_mod = std::make_shared<NetworkModule>();
    if (m_config.modules.system) m_sys_mod = std::make_shared<SystemModule>();
    if (m_config.modules.screenshot) m_shot_mod = std::make_shared<ScreenshotModule>();
    if (m_config.modules.notification) m_notif_mod = std::make_shared<NotificationModule>();
    if (m_config.modules.quickaction) m_quick_mod = std::make_shared<QuickActionModule>();

    m_modules = {
        m_clock_mod, m_ws_mod, m_win_mod, m_audio_mod, m_media_mod,
        m_bat_mod, m_net_mod, m_sys_mod, m_shot_mod, m_notif_mod, m_quick_mod
    };

    if (!m_wayland.init(m_config.idle_width, m_config.idle_height, m_config.margin_top)) {
        return false;
    }

    m_renderer.init(m_config);
    m_ipc_server.start();

    setup_callbacks();
    query_brightness();

    auto now = std::chrono::steady_clock::now();
    m_last_clock_update = now;
    m_last_media_update = now;
    m_last_slow_update = now;

    return true;
}

void App::query_brightness() {
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
    if (delta_percent >= 0) {
        std::system(("brightnessctl set " + std::to_string(delta_percent) + "%+ >/dev/null 2>&1 &").c_str());
    } else {
        std::system(("brightnessctl set " + std::to_string(-delta_percent) + "%- >/dev/null 2>&1 &").c_str());
    }
    query_brightness();
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
            render_current_state();
        }
    });

    m_hyprland_ipc.set_window_callback([this](const std::string& cls, const std::string& title) {
        if (m_win_mod) {
            m_win_mod->set_active_window(cls, title);
            render_current_state();
        }
    });

    m_hyprland_ipc.set_fullscreen_callback([this](bool fs) {
        m_is_fullscreen = fs;
        m_wayland.set_visible(!fs);
    });

    // CLI IPC Server callback
    m_ipc_server.set_command_callback([this](const std::string& cmd, const std::vector<std::string>& args) {
        return handle_ipc_command(cmd, args);
    });
}

void App::get_target_dimensions(IslandMode mode, double& w, double& h) {
    switch (mode) {
        case IslandMode::Idle:
            w = m_config.idle_width;
            h = m_config.idle_height;
            break;
        case IslandMode::Expanded_Audio:
        case IslandMode::Expanded_Brightness:
            w = 290.0;
            h = 48.0;
            break;
        case IslandMode::Expanded_Media:
            w = 380.0;
            h = 82.0;
            break;
        case IslandMode::Expanded_System:
            w = 340.0;
            h = 104.0;
            break;
        case IslandMode::Expanded_Clock:
            w = 320.0;
            h = 100.0;
            break;
        case IslandMode::Expanded_Network:
            w = 310.0;
            h = 76.0;
            break;
        case IslandMode::Expanded_Notification:
            w = 360.0;
            h = 66.0;
            break;
        case IslandMode::Expanded_Screenshot:
            w = 300.0;
            h = 50.0;
            break;
        case IslandMode::Expanded_QuickActions:
            w = 340.0;
            h = 76.0;
            break;
        default:
            w = m_config.idle_width;
            h = m_config.idle_height;
            break;
    }
}

void App::expand_to(IslandMode mode, int timeout_ms) {
    if (m_mode == mode && !m_animating) return;

    m_mode = mode;
    m_start_w = m_curr_w;
    m_start_h = m_curr_h;
    get_target_dimensions(m_mode, m_target_w, m_target_h);

    m_animating = true;
    m_anim_start = std::chrono::steady_clock::now();

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

    m_mode = IslandMode::Idle;
    m_has_auto_collapse = false;

    m_start_w = m_curr_w;
    m_start_h = m_curr_h;
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
                if (hb.action == "toggle_expand") {
                    if (hb.param == "clock") toggle_mode(IslandMode::Expanded_Clock);
                    else if (hb.param == "media") toggle_mode(IslandMode::Expanded_Media);
                    else if (hb.param == "system") toggle_mode(IslandMode::Expanded_System);
                    else if (hb.param == "network") toggle_mode(IslandMode::Expanded_Network);
                    else if (hb.param == "notification") toggle_mode(IslandMode::Expanded_Notification);
                    else if (hb.param == "quick") toggle_mode(IslandMode::Expanded_QuickActions);
                    return;
                }

                // Check modules for handler
                for (auto& mod : m_modules) {
                    if (mod && mod->handle_click(hb.action, hb.param)) {
                        render_current_state();
                        return;
                    }
                }
            }
        }

        // If clicked on island body but no specific button, toggle clock/idle
        if (m_mode != IslandMode::Idle) {
            collapse();
        }
    }
}

void App::on_mouse_motion(double x, double y) {}

void App::on_mouse_scroll(double delta) {
    if (m_audio_mod) {
        int step = (delta > 0) ? -5 : 5;
        m_audio_mod->change_volume(step);
        expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
    }
}

std::string App::handle_ipc_command(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "toggle") {
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
            if (target == "audio") expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
            else if (target == "brightness") expand_to(IslandMode::Expanded_Brightness, m_config.timeouts.brightness_ms);
            else if (target == "media") expand_to(IslandMode::Expanded_Media, 0);
            else if (target == "system") expand_to(IslandMode::Expanded_System, 0);
            else if (target == "clock") expand_to(IslandMode::Expanded_Clock, 0);
            else if (target == "network") expand_to(IslandMode::Expanded_Network, 0);
            else if (target == "quick") expand_to(IslandMode::Expanded_QuickActions, 0);
            return "OK";
        }
    }
    if (cmd == "volume") {
        if (!args.empty() && m_audio_mod) {
            std::string val = args[0];
            if (val == "toggle" || val == "mute") {
                m_audio_mod->toggle_mute();
            } else if (val.find('+') != std::string::npos) {
                int delta = 5;
                sscanf(val.c_str(), "%d", &delta);
                m_audio_mod->change_volume(std::abs(delta));
            } else if (val.find('-') != std::string::npos) {
                int delta = 5;
                sscanf(val.c_str(), "%d", &delta);
                m_audio_mod->change_volume(-std::abs(delta));
            }
            expand_to(IslandMode::Expanded_Audio, m_config.timeouts.volume_ms);
            return "OK";
        }
    }
    if (cmd == "brightness") {
        if (!args.empty()) {
            std::string val = args[0];
            if (val.find('+') != std::string::npos) {
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
    if (cmd == "reload") {
        m_config.load_from_file(Config::get_default_config_path());
        m_renderer.update_font(m_config);
        m_wayland.set_margin_top(m_config.margin_top);
        render_current_state();
        return "Config reloaded";
    }
    if (cmd == "quit") {
        stop();
        return "Stopping";
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
        } else if (m_mode == IslandMode::Expanded_System) {
            timeout_ms = 1500; // System monitor tick
        }

        m_wayland.prepare_read();
        int ret = poll(fds, nfds, timeout_ms);

        if (ret > 0) {
            bool wl_readable = false;
            for (int i = 0; i < nfds; ++i) {
                if (fds[i].fd == wl_fd && (fds[i].revents & POLLIN)) {
                    wl_readable = true;
                }
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

        auto now = std::chrono::steady_clock::now();

        // Check auto-collapse timer
        if (m_has_auto_collapse && now >= m_auto_collapse_time) {
            collapse();
        }

        // Periodic Clock update (every 1s)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_clock_update).count() >= 1) {
            m_last_clock_update = now;
            if (m_clock_mod) {
                m_clock_mod->update();
            }
            if (m_mode == IslandMode::Idle || m_mode == IslandMode::Expanded_Clock) {
                render_current_state();
            }
        }

        // Periodic System Monitor update (only when expanded!)
        if (m_mode == IslandMode::Expanded_System && m_sys_mod) {
            m_sys_mod->update();
            render_current_state();
        }

        // Periodic Media status update (every 2s)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_media_update).count() >= 2) {
            m_last_media_update = now;
            if (m_media_mod) {
                m_media_mod->update();
            }
            if (m_mode == IslandMode::Expanded_Media || m_mode == IslandMode::Idle) {
                render_current_state();
            }
        }

        // Periodic Slow update: Battery & Network (every 10s)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_last_slow_update).count() >= 10) {
            m_last_slow_update = now;
            if (m_bat_mod) m_bat_mod->update();
            if (m_net_mod) m_net_mod->update();
            if (m_mode == IslandMode::Idle) {
                render_current_state();
            }
        }
    }
}

void App::stop() {
    m_running = false;
    m_ipc_server.stop();
    m_hyprland_ipc.disconnect();
    m_wayland.cleanup();
}

} // namespace di
