#pragma once

#include <string>
#include <vector>
#include "json.hpp"

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

    ThemeColors colors;
    ModuleSettings modules;
    TimeoutSettings timeouts;

    bool load_from_file(const std::string& path);
    bool load_default();
    static std::string get_default_config_path();
};

} // namespace di
