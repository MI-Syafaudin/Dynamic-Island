#pragma once

#include <string>
#include <vector>
#include <utility>

namespace di {

struct ColorRGBA {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;

    static ColorRGBA from_hex(const std::string& hex, double alpha = 1.0);
};

struct ThemeColors {
    ColorRGBA background;
    ColorRGBA border;
    ColorRGBA text;
    ColorRGBA subtext;
    ColorRGBA accent;
    ColorRGBA accent_blue;
    ColorRGBA accent_purple;
    ColorRGBA warning;
    ColorRGBA danger;
    ColorRGBA card_bg;
};

struct ModuleSettings {
    bool clock = true;
    bool workspace = true;
    bool active_window = true;
    bool audio = true;
    bool media = true;
    bool battery = true;
    bool network = true;
    bool system = true;
    bool screenshot = true;
    bool notification = true;
    bool quickaction = true;
};

struct TimeoutSettings {
    int volume_ms = 2000;
    int brightness_ms = 2000;
    int notification_ms = 4500;
    int screenshot_ms = 2800;
    int auto_collapse_ms = 5000;
};

struct MonitorSettings {
    int system_expanded_interval_ms = 250;
    int system_idle_interval_ms = 2500;
    int network_expanded_interval_ms = 400;

    double cpu_smoothing_alpha = 0.80;
    double gpu_smoothing_alpha = 0.75;

    std::string gpu_name = "";
    std::string thermal_path = "";
    std::string ping_target = "1.1.1.1";
    std::string ping_label = "Cloudflare";
    std::string disk_path = "/";
};

class Config {
public:
    std::string position = "top-center";
    int margin_top = 8;
    int idle_width = 260;
    int idle_height = 34;
    int corner_radius = 17;
    double opacity = 0.92;
    int animation_duration_ms = 220;

    std::string font_family = "FiraCode Nerd Font, JetBrainsMono Nerd Font, Sans";
    int font_size = 11;

    std::string layer = "overlay";
    bool hide_on_fullscreen = false;
    bool adaptive_width = true;
    int max_idle_width = 850;
    int max_window_title_length = 28;
    int max_media_title_length = 28;
    std::string window_display_mode = "icon"; // "icon", "both", "text"
    std::string ping_target = "1.1.1.1";

    std::string theme = "default_dark";
    ThemeColors colors;
    ModuleSettings modules;
    TimeoutSettings timeouts;
    MonitorSettings monitor;

    // Custom or pre-resolved system binaries & sounds
    std::string audio_player;
    std::string sound_alarm;
    std::string sound_ting;
    std::string nightlight_binary;

    // Window icon mappings loaded from Lua: { pattern, icon }
    std::vector<std::pair<std::string, std::string>> window_icons;

    bool load_from_file(const std::string& path);
    bool load_default();
    void resolve_system_dependencies();
    static std::string get_default_config_path();
};

} // namespace di
