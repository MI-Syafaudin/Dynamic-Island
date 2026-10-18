-- ==============================================================================
-- Dynamic Island for Hyprland - Configuration (Lua)
-- ==============================================================================

local config = {}

-- ------------------------------------------------------------------------------
-- Geometry & Appearance
-- ------------------------------------------------------------------------------
config.position = "top-center"
config.margin_top = 8
config.idle_width = 260
config.idle_height = 34
config.corner_radius = 17
config.opacity = 0.92
config.animation_duration_ms = 220
config.font_family = "FiraCode Nerd Font, JetBrainsMono Nerd Font, Sans"
config.font_size = 11
config.layer = "overlay"
config.hide_on_fullscreen = false
config.adaptive_width = true
config.max_idle_width = 850
config.max_window_title_length = 28
config.max_media_title_length = 28
config.window_display_mode = "icon" -- "icon", "both", "text"
config.ping_target = "1.1.1.1"

-- ------------------------------------------------------------------------------
-- Active Theme & Built-in Themes
-- ------------------------------------------------------------------------------
config.theme = "default_dark"

config.themes = {
    default_dark = {
        background    = "#111111",
        border        = "#2c2c2e",
        text          = "#ffffff",
        subtext       = "#8e8e93",
        accent        = "#38ef7d",
        accent_blue   = "#0a84ff",
        accent_purple = "#bf5af2",
        warning       = "#ffd60a",
        danger        = "#ff453a",
        card_bg       = "#1c1c1e",
    },
    amoled_black = {
        background    = "#000000",
        border        = "#222222",
        text          = "#ffffff",
        subtext       = "#999999",
        accent        = "#ffffff",
        accent_blue   = "#0a84ff",
        accent_purple = "#bf5af2",
        warning       = "#ffd60a",
        danger        = "#ff453a",
        card_bg       = "#111111",
    },
    emerald_dark = {
        background    = "#0b1411",
        border        = "#1a2c25",
        text          = "#e6f4ea",
        subtext       = "#7ba892",
        accent        = "#2ecc71",
        accent_blue   = "#27ae60",
        accent_purple = "#a55eea",
        warning       = "#f39c12",
        danger        = "#e74c3c",
        card_bg       = "#12201b",
    },
    midnight_blue = {
        background    = "#0b0f19",
        border        = "#1c2538",
        text          = "#f0f6fc",
        subtext       = "#7d8590",
        accent        = "#58a6ff",
        accent_blue   = "#1f6feb",
        accent_purple = "#bc8cff",
        warning       = "#e3b341",
        danger        = "#f85149",
        card_bg       = "#161b22",
    },
}

-- Custom color overrides (set to a table to override theme colors)
config.colors = nil

-- ------------------------------------------------------------------------------
-- Enabled Modules
-- ------------------------------------------------------------------------------
config.modules = {
    clock         = true,
    workspace     = true,
    active_window = true,
    audio         = true,
    media         = true,
    battery       = true,
    network       = true,
    system        = true,
    screenshot    = true,
    notification  = true,
    quickaction   = true,
}

-- ------------------------------------------------------------------------------
-- Timeouts (milliseconds)
-- ------------------------------------------------------------------------------
config.timeouts = {
    volume_ms        = 2000,
    brightness_ms    = 2000,
    notification_ms  = 4500,
    screenshot_ms    = 2800,
    auto_collapse_ms = 5000,
}

-- ------------------------------------------------------------------------------
-- Audio & Nightlight Execution Settings
-- Leave empty ("") for automatic pre-resolving at startup
-- ------------------------------------------------------------------------------
config.audio_player       = "" -- e.g. "pw-play", "paplay", "canberra-gtk-play", "aplay"
config.sound_alarm         = "" -- path to .oga / .wav file
config.sound_ting          = "" -- path to .oga / .wav file
config.nightlight_binary   = "" -- "hyprsunset", "wlsunset", "gammastep"

-- ------------------------------------------------------------------------------
-- Application Window Icon Mapping
-- Matches substring in app class or window title (case-insensitive)
-- ------------------------------------------------------------------------------
config.window_icons = {
    -- Terminal / Shell
    { pattern = "kitty",         icon = "" },
    { pattern = "alacritty",     icon = "" },
    { pattern = "foot",          icon = "" },
    { pattern = "wezterm",       icon = "" },
    { pattern = "ghostty",       icon = "" },
    { pattern = "konsole",       icon = "" },
    { pattern = "terminal",      icon = "" },
    { pattern = "terminator",    icon = "" },
    { pattern = "tilix",         icon = "" },
    { pattern = "urxvt",         icon = "" },
    { pattern = "xterm",         icon = "" },
    { pattern = "st",            icon = "" },

    -- Web Browsers
    { pattern = "firefox",       icon = "󰈹" },
    { pattern = "librewolf",     icon = "󰈹" },
    { pattern = "waterfox",      icon = "󰈹" },
    { pattern = "zen",           icon = "󰈹" },
    { pattern = "chrome",        icon = "󰊯" },
    { pattern = "chromium",      icon = "󰊯" },
    { pattern = "brave",         icon = "󰖟" },
    { pattern = "edge",          icon = "󰇩" },
    { pattern = "opera",         icon = "󰖟" },
    { pattern = "vivaldi",       icon = "󰖟" },
    { pattern = "tor",           icon = "󰖟" },
    { pattern = "browser",       icon = "󰖟" },

    -- Editors & Development
    { pattern = "code",          icon = "󰨞" },
    { pattern = "vscode",        icon = "󰨞" },
    { pattern = "vscodium",      icon = "󰨞" },
    { pattern = "nvim",          icon = "" },
    { pattern = "neovim",        icon = "" },
    { pattern = "vim",           icon = "" },
    { pattern = "emacs",         icon = "" },
    { pattern = "sublime",       icon = "󰅳" },
    { pattern = "idea",          icon = "󰨞" },
    { pattern = "clion",         icon = "󰨞" },
    { pattern = "pycharm",       icon = "󰨞" },
    { pattern = "webstorm",      icon = "󰨞" },
    { pattern = "studio",        icon = "󰨞" },
    { pattern = "jetbrains",     icon = "󰨞" },
    { pattern = "git",           icon = "󰊤" },
    { pattern = "github",        icon = "󰊤" },
    { pattern = "docker",        icon = "󰡨" },
    { pattern = "postman",       icon = "󰲝" },
    { pattern = "insomnia",      icon = "󰲝" },
    { pattern = "gedit",         icon = "󰅳" },
    { pattern = "kate",          icon = "󰅳" },
    { pattern = "mousepad",      icon = "󰅳" },
    { pattern = "leafpad",       icon = "󰅳" },
    { pattern = "text-editor",   icon = "󰅳" },
    { pattern = "notepad",       icon = "󰅳" },

    -- File Managers
    { pattern = "thunar",        icon = "󰉋" },
    { pattern = "nautilus",      icon = "󰉋" },
    { pattern = "dolphin",       icon = "󰉋" },
    { pattern = "nemo",          icon = "󰉋" },
    { pattern = "pcmanfm",       icon = "󰉋" },
    { pattern = "ranger",        icon = "󰉋" },
    { pattern = "yazi",          icon = "󰉋" },
    { pattern = "files",         icon = "󰉋" },

    -- Media & Creative
    { pattern = "spotify",       icon = "󰓇" },
    { pattern = "vlc",           icon = "󰕼" },
    { pattern = "mpv",           icon = "󰐌" },
    { pattern = "youtube",       icon = "󰗃" },
    { pattern = "obs",           icon = "󰑋" },
    { pattern = "gimp",          icon = "󰽉" },
    { pattern = "inkscape",      icon = "󰽉" },
    { pattern = "krita",         icon = "󰽉" },
    { pattern = "blender",       icon = "󰽉" },
    { pattern = "figma",         icon = "󰽉" },
    { pattern = "audacity",      icon = "󰝚" },

    -- Social & Communication
    { pattern = "discord",       icon = "󰙯" },
    { pattern = "vesktop",       icon = "󰙯" },
    { pattern = "webcord",       icon = "󰙯" },
    { pattern = "telegram",      icon = "󰏲" },
    { pattern = "ayugram",       icon = "󰏲" },
    { pattern = "slack",         icon = "󰒱" },
    { pattern = "whatsapp",      icon = "󰖣" },
    { pattern = "signal",        icon = "󰭹" },
    { pattern = "thunderbird",   icon = "󰇮" },
    { pattern = "geary",         icon = "󰇮" },
    { pattern = "mail",          icon = "󰇮" },

    -- Gaming
    { pattern = "steam",         icon = "󰓓" },
    { pattern = "lutris",        icon = "󰓓" },
    { pattern = "heroic",        icon = "󰓓" },
    { pattern = "minecraft",     icon = "󰍳" },
    { pattern = "prism",         icon = "󰍳" },

    -- Documents & Notes
    { pattern = "obsidian",      icon = "󰠮" },
    { pattern = "notion",        icon = "󰠮" },
    { pattern = "libreoffice",   icon = "󰈙" },
    { pattern = "soffice",       icon = "󰈙" },
    { pattern = "writer",        icon = "󰈙" },
    { pattern = "calc",          icon = "󰈙" },
    { pattern = "evince",        icon = "󰈦" },
    { pattern = "zathura",       icon = "󰈦" },
    { pattern = "okular",        icon = "󰈦" },
    { pattern = "pdf",           icon = "󰈦" },

    -- System Tools
    { pattern = "setting",        icon = "󰒓" },
    { pattern = "control-center", icon = "󰒓" },
    { pattern = "pavucontrol",   icon = "󰕾" },
    { pattern = "audio",         icon = "󰕾" },
    { pattern = "btop",          icon = "󰾴" },
    { pattern = "htop",          icon = "󰾴" },
    { pattern = "system-monitor",icon = "󰾴" },
    { pattern = "calculator",    icon = "󰃬" },
    { pattern = "kcalc",         icon = "󰃬" },
    { pattern = "virtualbox",    icon = "󰢹" },
    { pattern = "virt-manager",  icon = "󰢹" },
    { pattern = "rofi",          icon = "󰍜" },
    { pattern = "wofi",          icon = "󰍜" },
}

return config
