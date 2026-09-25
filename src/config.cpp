#include "config.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>

namespace di {

ColorRGBA ColorRGBA::from_hex(const std::string& hex, double alpha) {
    ColorRGBA c;
    c.a = alpha;
    if (hex.empty()) return c;

    std::string clean = hex;
    if (clean[0] == '#') clean = clean.substr(1);

    if (clean.length() == 6) {
        unsigned int val = 0;
        std::stringstream ss;
        ss << std::hex << clean;
        ss >> val;
        c.r = ((val >> 16) & 0xFF) / 255.0;
        c.g = ((val >> 8) & 0xFF) / 255.0;
        c.b = (val & 0xFF) / 255.0;
    } else if (clean.length() == 8) {
        unsigned int val = 0;
        std::stringstream ss;
        ss << std::hex << clean;
        ss >> val;
        c.r = ((val >> 24) & 0xFF) / 255.0;
        c.g = ((val >> 16) & 0xFF) / 255.0;
        c.b = ((val >> 8) & 0xFF) / 255.0;
        c.a = (val & 0xFF) / 255.0;
    }
    return c;
}

std::string Config::get_default_config_path() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::string(xdg_config) + "/dynamic-island/config.json";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/dynamic-island/config.json";
    }
    return "config.json";
}

bool Config::load_default() {
    position = "top-center";
    margin_top = 8;
    idle_width = 260;
    idle_height = 34;
    corner_radius = 17;
    opacity = 0.92;
    animation_duration_ms = 220;

    font_family = "FiraCode Nerd Font, JetBrainsMono Nerd Font, Sans";
    font_size = 11;

    layer = "overlay";
    hide_on_fullscreen = false;
    adaptive_width = true;
    max_idle_width = 850;
    max_window_title_length = 28;

    colors.background = ColorRGBA::from_hex("#111111", opacity);
    colors.border = ColorRGBA::from_hex("#2c2c2e", 0.7);
    colors.text = ColorRGBA::from_hex("#ffffff", 1.0);
    colors.subtext = ColorRGBA::from_hex("#8e8e93", 1.0);
    colors.accent = ColorRGBA::from_hex("#38ef7d", 1.0);
    colors.accent_blue = ColorRGBA::from_hex("#0a84ff", 1.0);
    colors.warning = ColorRGBA::from_hex("#ffd60a", 1.0);
    colors.danger = ColorRGBA::from_hex("#ff453a", 1.0);
    colors.card_bg = ColorRGBA::from_hex("#1c1c1e", 0.6);

    modules = ModuleSettings();
    timeouts = TimeoutSettings();
    return true;
}

bool Config::load_from_file(const std::string& path) {
    load_default();

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    JsonValue root = JsonParser::parse(content);
    if (!root.is_object()) {
        return false;
    }

    if (root.has("position")) position = root["position"].as_string(position);
    if (root.has("margin_top")) margin_top = root["margin_top"].as_int(margin_top);
    if (root.has("idle_width")) idle_width = root["idle_width"].as_int(idle_width);
    if (root.has("idle_height")) idle_height = root["idle_height"].as_int(idle_height);
    if (root.has("corner_radius")) corner_radius = root["corner_radius"].as_int(corner_radius);
    if (root.has("opacity")) opacity = root["opacity"].as_double(opacity);
    if (root.has("animation_duration_ms")) animation_duration_ms = root["animation_duration_ms"].as_int(animation_duration_ms);
    if (root.has("font_family")) font_family = root["font_family"].as_string(font_family);
    if (root.has("font_size")) font_size = root["font_size"].as_int(font_size);

    if (root.has("layer")) layer = root["layer"].as_string(layer);
    if (root.has("hide_on_fullscreen")) hide_on_fullscreen = root["hide_on_fullscreen"].as_bool(hide_on_fullscreen);
    if (root.has("adaptive_width")) adaptive_width = root["adaptive_width"].as_bool(adaptive_width);
    if (root.has("max_idle_width")) max_idle_width = root["max_idle_width"].as_int(max_idle_width);
    if (root.has("max_window_title_length")) max_window_title_length = root["max_window_title_length"].as_int(max_window_title_length);

    if (root.has("colors") && root["colors"].is_object()) {
        const auto& c = root["colors"];
        if (c.has("background")) colors.background = ColorRGBA::from_hex(c["background"].as_string(), opacity);
        if (c.has("border")) colors.border = ColorRGBA::from_hex(c["border"].as_string(), 0.7);
        if (c.has("text")) colors.text = ColorRGBA::from_hex(c["text"].as_string(), 1.0);
        if (c.has("subtext")) colors.subtext = ColorRGBA::from_hex(c["subtext"].as_string(), 1.0);
        if (c.has("accent")) colors.accent = ColorRGBA::from_hex(c["accent"].as_string(), 1.0);
        if (c.has("accent_blue")) colors.accent_blue = ColorRGBA::from_hex(c["accent_blue"].as_string(), 1.0);
        if (c.has("warning")) colors.warning = ColorRGBA::from_hex(c["warning"].as_string(), 1.0);
        if (c.has("danger")) colors.danger = ColorRGBA::from_hex(c["danger"].as_string(), 1.0);
        if (c.has("card_bg")) colors.card_bg = ColorRGBA::from_hex(c["card_bg"].as_string(), 0.6);
    }

    if (root.has("modules") && root["modules"].is_object()) {
        const auto& m = root["modules"];
        if (m.has("clock")) modules.clock = m["clock"].as_bool(modules.clock);
        if (m.has("workspace")) modules.workspace = m["workspace"].as_bool(modules.workspace);
        if (m.has("active_window")) modules.active_window = m["active_window"].as_bool(modules.active_window);
        if (m.has("audio")) modules.audio = m["audio"].as_bool(modules.audio);
        if (m.has("media")) modules.media = m["media"].as_bool(modules.media);
        if (m.has("battery")) modules.battery = m["battery"].as_bool(modules.battery);
        if (m.has("network")) modules.network = m["network"].as_bool(modules.network);
        if (m.has("system")) modules.system = m["system"].as_bool(modules.system);
        if (m.has("screenshot")) modules.screenshot = m["screenshot"].as_bool(modules.screenshot);
        if (m.has("notification")) modules.notification = m["notification"].as_bool(modules.notification);
        if (m.has("quickaction")) modules.quickaction = m["quickaction"].as_bool(modules.quickaction);
    }

    if (root.has("timeouts") && root["timeouts"].is_object()) {
        const auto& t = root["timeouts"];
        if (t.has("volume_ms")) timeouts.volume_ms = t["volume_ms"].as_int(timeouts.volume_ms);
        if (t.has("brightness_ms")) timeouts.brightness_ms = t["brightness_ms"].as_int(timeouts.brightness_ms);
        if (t.has("notification_ms")) timeouts.notification_ms = t["notification_ms"].as_int(timeouts.notification_ms);
        if (t.has("screenshot_ms")) timeouts.screenshot_ms = t["screenshot_ms"].as_int(timeouts.screenshot_ms);
        if (t.has("auto_collapse_ms")) timeouts.auto_collapse_ms = t["auto_collapse_ms"].as_int(timeouts.auto_collapse_ms);
    }

    return true;
}

} // namespace di
