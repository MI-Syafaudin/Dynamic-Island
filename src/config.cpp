#include "config.hpp"
#include <lua.hpp>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <unistd.h>
#include <algorithm>

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
        std::string p = std::string(xdg_config) + "/dynamic-island/config.lua";
        if (access(p.c_str(), R_OK) == 0) return p;
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::string p = std::string(home) + "/.config/dynamic-island/config.lua";
        if (access(p.c_str(), R_OK) == 0) return p;
    }
    if (access("config/config.lua", R_OK) == 0) {
        return "config/config.lua";
    }
    if (access("config.lua", R_OK) == 0) {
        return "config.lua";
    }
    if (home && *home) {
        return std::string(home) + "/.config/dynamic-island/config.lua";
    }
    return "config/config.lua";
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
    max_media_title_length = 28;
    window_display_mode = "icon";
    ping_target = "1.1.1.1";

    theme = "default_dark";

    colors.background = ColorRGBA::from_hex("#111111", opacity);
    colors.border = ColorRGBA::from_hex("#2c2c2e", 0.7);
    colors.text = ColorRGBA::from_hex("#ffffff", 1.0);
    colors.subtext = ColorRGBA::from_hex("#8e8e93", 1.0);
    colors.accent = ColorRGBA::from_hex("#38ef7d", 1.0);
    colors.accent_blue = ColorRGBA::from_hex("#0a84ff", 1.0);
    colors.accent_purple = ColorRGBA::from_hex("#bf5af2", 1.0);
    colors.warning = ColorRGBA::from_hex("#ffd60a", 1.0);
    colors.danger = ColorRGBA::from_hex("#ff453a", 1.0);
    colors.card_bg = ColorRGBA::from_hex("#1c1c1e", 0.6);

    modules = ModuleSettings();
    timeouts = TimeoutSettings();

    // Default basic window icons
    if (window_icons.empty()) {
        window_icons = {
            {"kitty", ""}, {"alacritty", ""}, {"foot", ""}, {"wezterm", ""},
            {"ghostty", ""}, {"konsole", ""}, {"terminal", ""},
            {"firefox", "󰈹"}, {"librewolf", "󰈹"}, {"chrome", "󰊯"}, {"chromium", "󰊯"},
            {"brave", "󰖟"}, {"code", "󰨞"}, {"vscode", "󰨞"}, {"nvim", ""}, {"vim", ""},
            {"thunar", "󰉋"}, {"nautilus", "󰉋"}, {"spotify", "󰓇"}, {"discord", "󰙯"}
        };
    }

    resolve_system_dependencies();
    return true;
}

void Config::resolve_system_dependencies() {
    // 1. Audio player pre-resolving
    if (audio_player.empty() || audio_player == "auto") {
        const char* players[] = {"pw-play", "paplay", "canberra-gtk-play", "aplay", "ffplay"};
        for (const char* p : players) {
            std::string p1 = std::string("/usr/bin/") + p;
            if (access(p1.c_str(), X_OK) == 0) {
                audio_player = p1;
                break;
            }
            std::string p2 = std::string("/usr/local/bin/") + p;
            if (access(p2.c_str(), X_OK) == 0) {
                audio_player = p2;
                break;
            }
        }
        if (audio_player.empty() || audio_player == "auto") {
            audio_player = "pw-play";
        }
    }

    // 2. Alarm sound pre-resolving
    if (sound_alarm.empty()) {
        const char* paths[] = {
            "/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga",
            "/usr/share/sounds/freedesktop/stereo/complete.oga",
            "/usr/share/sounds/freedesktop/stereo/bell.oga",
            "/usr/share/sounds/alsa/Front_Center.wav"
        };
        for (const char* path : paths) {
            if (access(path, R_OK) == 0) {
                sound_alarm = path;
                break;
            }
        }
    }

    // 3. Ting sound pre-resolving
    if (sound_ting.empty()) {
        const char* paths[] = {
            "/usr/share/sounds/freedesktop/stereo/complete.oga",
            "/usr/share/sounds/freedesktop/stereo/bell.oga",
            "/usr/share/sounds/alsa/Front_Center.wav"
        };
        for (const char* path : paths) {
            if (access(path, R_OK) == 0) {
                sound_ting = path;
                break;
            }
        }
    }

    // 4. Nightlight binary pre-resolving
    if (nightlight_binary.empty() || nightlight_binary == "auto") {
        const char* nls[] = {"hyprsunset", "wlsunset", "gammastep"};
        for (const char* nl : nls) {
            std::string p1 = std::string("/usr/bin/") + nl;
            if (access(p1.c_str(), X_OK) == 0) {
                nightlight_binary = p1;
                break;
            }
            std::string p2 = std::string("/usr/local/bin/") + nl;
            if (access(p2.c_str(), X_OK) == 0) {
                nightlight_binary = p2;
                break;
            }
        }
    }
}

static std::string get_lua_string(lua_State* L, const char* key, const std::string& def) {
    lua_getfield(L, -1, key);
    std::string val = def;
    if (lua_isstring(L, -1)) {
        val = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    return val;
}

static int get_lua_int(lua_State* L, const char* key, int def) {
    lua_getfield(L, -1, key);
    int val = def;
    if (lua_isnumber(L, -1)) {
        val = static_cast<int>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return val;
}

static double get_lua_double(lua_State* L, const char* key, double def) {
    lua_getfield(L, -1, key);
    double val = def;
    if (lua_isnumber(L, -1)) {
        val = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return val;
}

static bool get_lua_bool(lua_State* L, const char* key, bool def) {
    lua_getfield(L, -1, key);
    bool val = def;
    if (lua_isboolean(L, -1)) {
        val = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    return val;
}

static void apply_color_table(lua_State* L, ThemeColors& colors, double opacity) {
    std::string s;
    s = get_lua_string(L, "background", "");
    if (!s.empty()) colors.background = ColorRGBA::from_hex(s, opacity);

    s = get_lua_string(L, "border", "");
    if (!s.empty()) colors.border = ColorRGBA::from_hex(s, 0.7);

    s = get_lua_string(L, "text", "");
    if (!s.empty()) colors.text = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "subtext", "");
    if (!s.empty()) colors.subtext = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "accent", "");
    if (!s.empty()) colors.accent = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "accent_blue", "");
    if (!s.empty()) colors.accent_blue = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "accent_purple", "");
    if (!s.empty()) colors.accent_purple = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "warning", "");
    if (!s.empty()) colors.warning = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "danger", "");
    if (!s.empty()) colors.danger = ColorRGBA::from_hex(s, 1.0);

    s = get_lua_string(L, "card_bg", "");
    if (!s.empty()) colors.card_bg = ColorRGBA::from_hex(s, 0.6);
}

bool Config::load_from_file(const std::string& path) {
    load_default();

    if (access(path.c_str(), R_OK) != 0) {
        return false;
    }

    // Ephemeral Lua State: opened only during configuration reading, closed immediately
    lua_State* L = luaL_newstate();
    if (!L) return false;
    luaL_openlibs(L);

    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Config] Error executing Lua config (" << path << "): "
                  << (err ? err : "unknown error") << std::endl;
        lua_close(L);
        return false;
    }

    // If file returned a table, it is on top of stack. Otherwise check global 'config'
    if (!lua_istable(L, -1)) {
        lua_getglobal(L, "config");
        if (!lua_istable(L, -1)) {
            std::cerr << "[Config] Lua config did not return a table or define global 'config'." << std::endl;
            lua_close(L);
            return false;
        }
    }

    position = get_lua_string(L, "position", position);
    margin_top = get_lua_int(L, "margin_top", margin_top);
    idle_width = get_lua_int(L, "idle_width", idle_width);
    idle_height = get_lua_int(L, "idle_height", idle_height);
    corner_radius = get_lua_int(L, "corner_radius", corner_radius);
    opacity = get_lua_double(L, "opacity", opacity);
    animation_duration_ms = get_lua_int(L, "animation_duration_ms", animation_duration_ms);

    font_family = get_lua_string(L, "font_family", font_family);
    font_size = get_lua_int(L, "font_size", font_size);

    layer = get_lua_string(L, "layer", layer);
    hide_on_fullscreen = get_lua_bool(L, "hide_on_fullscreen", hide_on_fullscreen);
    adaptive_width = get_lua_bool(L, "adaptive_width", adaptive_width);
    max_idle_width = get_lua_int(L, "max_idle_width", max_idle_width);
    max_window_title_length = get_lua_int(L, "max_window_title_length", max_window_title_length);
    max_media_title_length = get_lua_int(L, "max_media_title_length", max_media_title_length);
    window_display_mode = get_lua_string(L, "window_display_mode", window_display_mode);
    ping_target = get_lua_string(L, "ping_target", ping_target);

    // Audio / sounds
    audio_player = get_lua_string(L, "audio_player", audio_player);
    sound_alarm = get_lua_string(L, "sound_alarm", sound_alarm);
    sound_ting = get_lua_string(L, "sound_ting", sound_ting);
    nightlight_binary = get_lua_string(L, "nightlight_binary", nightlight_binary);

    // Active theme & built-in themes table
    theme = get_lua_string(L, "theme", theme);
    lua_getfield(L, -1, "themes");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, theme.c_str());
        if (lua_istable(L, -1)) {
            apply_color_table(L, colors, opacity);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // Custom color overrides if present
    lua_getfield(L, -1, "colors");
    if (lua_istable(L, -1)) {
        apply_color_table(L, colors, opacity);
    }
    lua_pop(L, 1);

    // Modules
    lua_getfield(L, -1, "modules");
    if (lua_istable(L, -1)) {
        modules.clock = get_lua_bool(L, "clock", modules.clock);
        modules.workspace = get_lua_bool(L, "workspace", modules.workspace);
        modules.active_window = get_lua_bool(L, "active_window", modules.active_window);
        modules.audio = get_lua_bool(L, "audio", modules.audio);
        modules.media = get_lua_bool(L, "media", modules.media);
        modules.battery = get_lua_bool(L, "battery", modules.battery);
        modules.network = get_lua_bool(L, "network", modules.network);
        modules.system = get_lua_bool(L, "system", modules.system);
        modules.screenshot = get_lua_bool(L, "screenshot", modules.screenshot);
        modules.notification = get_lua_bool(L, "notification", modules.notification);
        modules.quickaction = get_lua_bool(L, "quickaction", modules.quickaction);
    }
    lua_pop(L, 1);

    // Timeouts
    lua_getfield(L, -1, "timeouts");
    if (lua_istable(L, -1)) {
        timeouts.volume_ms = get_lua_int(L, "volume_ms", timeouts.volume_ms);
        timeouts.brightness_ms = get_lua_int(L, "brightness_ms", timeouts.brightness_ms);
        timeouts.notification_ms = get_lua_int(L, "notification_ms", timeouts.notification_ms);
        timeouts.screenshot_ms = get_lua_int(L, "screenshot_ms", timeouts.screenshot_ms);
        timeouts.auto_collapse_ms = get_lua_int(L, "auto_collapse_ms", timeouts.auto_collapse_ms);
    }
    lua_pop(L, 1);

    // Window icon mappings: array of { pattern = "...", icon = "..." }
    lua_getfield(L, -1, "window_icons");
    if (lua_istable(L, -1)) {
        window_icons.clear();
        int len = static_cast<int>(lua_objlen(L, -1));
        for (int i = 1; i <= len; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_istable(L, -1)) {
                std::string pat = get_lua_string(L, "pattern", "");
                std::string ico = get_lua_string(L, "icon", "");
                if (!pat.empty() && !ico.empty()) {
                    window_icons.emplace_back(pat, ico);
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Immediately close Lua State: runtime RAM overhead = 0 KB, idle CPU = 0.0%
    lua_close(L);

    resolve_system_dependencies();
    return true;
}

} // namespace di
