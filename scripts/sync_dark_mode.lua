#!/usr/bin/env luajit
-- ==============================================================================
-- Sync dark mode between desktop, GTK, portals, and web browsers (LuaJIT)
-- Zero python overhead, ultra-fast file operations
-- ==============================================================================

local action = arg[1] or "toggle"
local home = os.getenv("HOME") or ""

local function is_dark()
    local f = io.popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null")
    if f then
        local out = f:read("*l") or ""
        f:close()
        if out:find("prefer%-dark") then return true end
        if out:find("prefer%-light") then return false end
    end

    local f_gtk = io.popen("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null")
    if f_gtk then
        local out = (f_gtk:read("*l") or ""):lower()
        f_gtk:close()
        if out:find("dark") then return true end
    end

    local f_ini = io.open(home .. "/.config/gtk-3.0/settings.ini", "r")
    if f_ini then
        local content = f_ini:read("*a")
        f_ini:close()
        if content:find("gtk%-application%-prefer%-dark%-theme%s*=%s*1") then
            return true
        end
    end

    return false
end

if action == "status" then
    print(is_dark() and "dark" or "light")
    os.exit(0)
end

local target_dark = false
if action == "on" or action == "true" or action == "1" or action == "dark" then
    target_dark = true
elseif action == "off" or action == "false" or action == "0" or action == "light" then
    target_dark = false
elseif action == "toggle" then
    target_dark = not is_dark()
end

local color_scheme = target_dark and "prefer-dark" or "prefer-light"
local gtk_theme = target_dark and "Adwaita-dark" or "Adwaita"
local dark_num = target_dark and 1 or 0
local theme_id = target_dark and "firefox-compact-dark@mozilla.org" or "firefox-compact-light@mozilla.org"

-- 1. Update gsettings & dconf
os.execute("gsettings set org.gnome.desktop.interface color-scheme '" .. color_scheme .. "' 2>/dev/null &")
os.execute("gsettings set org.gnome.desktop.interface gtk-theme '" .. gtk_theme .. "' 2>/dev/null &")
os.execute("if command -v dconf >/dev/null 2>&1; then " ..
           "dconf write /org/gnome/desktop/interface/color-scheme \"'" .. color_scheme .. "'\" 2>/dev/null & " ..
           "dconf write /org/gnome/desktop/interface/gtk-theme \"'" .. gtk_theme .. "'\" 2>/dev/null & fi")

-- 2. Update GTK 3.0 & GTK 4.0 settings.ini
local function update_gtk_ini(path)
    local dir = path:match("(.+)/[^/]+$")
    if dir then os.execute("mkdir -p '" .. dir .. "'") end

    local content = ""
    local f = io.open(path, "r")
    if f then
        content = f:read("*a")
        f:close()
    end

    if content == "" or not content:find("%[Settings%]") then
        content = "[Settings]\ngtk-application-prefer-dark-theme=" .. dark_num .. "\ngtk-theme-name=" .. gtk_theme .. "\n"
    else
        if content:find("gtk%-application%-prefer%-dark%-theme") then
            content = content:gsub("gtk%-application%-prefer%-dark%-theme%s*=[^\n]*", "gtk-application-prefer-dark-theme=" .. dark_num)
        else
            content = content .. "\ngtk-application-prefer-dark-theme=" .. dark_num
        end
        if content:find("gtk%-theme%-name") then
            content = content:gsub("gtk%-theme%-name%s*=[^\n]*", "gtk-theme-name=" .. gtk_theme)
        else
            content = content .. "\ngtk-theme-name=" .. gtk_theme
        end
    end

    local out = io.open(path, "w")
    if out then
        out:write(content)
        out:close()
    end
end

update_gtk_ini(home .. "/.config/gtk-3.0/settings.ini")
update_gtk_ini(home .. "/.config/gtk-4.0/settings.ini")

-- 3. LibreWolf overrides
local lwo_content = string.format([[
// LibreWolf Overrides for Theme & Dark Mode Sync with OS / Dynamic Island
defaultPref("privacy.resistFingerprinting", false);
defaultPref("privacy.fingerprintingProtection", true);
defaultPref("privacy.fingerprintingProtection.overrides", "+AllTargets,-CSSPrefersColorScheme");
defaultPref("layout.css.prefers-color-scheme.content-override", 2);
defaultPref("browser.theme.content-theme", 2);
defaultPref("browser.theme.toolbar-theme", 2);
defaultPref("ui.systemUsesDarkTheme", %d);
]], dark_num)

local function write_file(path, str)
    local dir = path:match("(.+)/[^/]+$")
    if dir then os.execute("mkdir -p '" .. dir .. "'") end
    local f = io.open(path, "w")
    if f then
        f:write(str)
        f:close()
    end
end

write_file(home .. "/.librewolf/librewolf.overrides.cfg", lwo_content)
write_file(home .. "/.config/librewolf/librewolf/librewolf.overrides.cfg", lwo_content)

-- 4. Firefox / LibreWolf user.js update
local function update_user_js(pdir)
    local ujs = pdir .. "/user.js"
    local lines = {}
    local f = io.open(ujs, "r")
    if f then
        for line in f:lines() do
            if not line:find("privacy%.resistFingerprinting") and
               not line:find("privacy%.fingerprintingProtection") and
               not line:find("layout%.css%.prefers%-color%-scheme") and
               not line:find("browser%.theme") and
               not line:find("ui%.systemUsesDarkTheme") and
               not line:find("extensions%.activeThemeID") then
                table.insert(lines, line)
            end
        end
        f:close()
    end

    table.insert(lines, 'user_pref("privacy.resistFingerprinting", false);')
    table.insert(lines, 'user_pref("privacy.fingerprintingProtection", true);')
    table.insert(lines, 'user_pref("privacy.fingerprintingProtection.overrides", "+AllTargets,-CSSPrefersColorScheme");')
    table.insert(lines, 'user_pref("layout.css.prefers-color-scheme.content-override", 2);')
    table.insert(lines, 'user_pref("browser.theme.content-theme", 2);')
    table.insert(lines, 'user_pref("browser.theme.toolbar-theme", 2);')
    table.insert(lines, 'user_pref("ui.systemUsesDarkTheme", ' .. dark_num .. ');')
    table.insert(lines, 'user_pref("extensions.activeThemeID", "' .. theme_id .. '");')

    local out = io.open(ujs, "w")
    if out then
        out:write(table.concat(lines, "\n") .. "\n")
        out:close()
    end
end

local p = io.popen("find '" .. home .. "/.config/librewolf/librewolf' '" .. home .. "/.config/mozilla/firefox' '" .. home .. "/.mozilla/firefox' -maxdepth 1 -mindepth 1 -type d 2>/dev/null")
if p then
    for dir in p:lines() do
        local test_pref = io.open(dir .. "/prefs.js", "r")
        if test_pref then
            test_pref:close()
            update_user_js(dir)
        end
    end
    p:close()
end

print(target_dark and "Dark mode: ON" or "Dark mode: OFF")
