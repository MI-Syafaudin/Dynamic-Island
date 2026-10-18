#!/usr/bin/env luajit
-- ==============================================================================
-- Dynamic Island Power Menu (LuaJIT)
-- Integrates with wlogout, rofi, wofi, hyprlock, and systemctl
-- ==============================================================================

local function command_exists(cmd)
    local f = io.popen("command -v " .. cmd .. " 2>/dev/null")
    if not f then return false end
    local res = f:read("*a")
    f:close()
    return res ~= nil and #res > 0
end

if command_exists("wlogout") then
    os.execute("wlogout -b 2 &")
    os.exit(0)
end

local options = "󰐥  Shutdown\n󰜉  Restart\n󰌾  Lock Screen\n󰤄  Suspend\n󰗽  Log Out"
local choice = nil

if command_exists("rofi") then
    local p = io.popen("printf '%b' '" .. options .. "' | rofi -dmenu -i -p 'Power Menu' -theme-str 'window {width: 280px; border-radius: 12px;} listview {lines: 5;}' 2>/dev/null")
    if p then
        choice = p:read("*l")
        p:close()
    end
elseif command_exists("wofi") then
    local p = io.popen("printf '%b' '" .. options .. "' | wofi --dmenu --prompt 'Power Menu' --width 280 --lines 5 2>/dev/null")
    if p then
        choice = p:read("*l")
        p:close()
    end
else
    os.execute("systemctl poweroff &")
    os.exit(0)
end

if not choice or choice == "" then
    os.exit(0)
end

if choice:find("Shutdown") then
    os.execute("systemctl poweroff &")
elseif choice:find("Restart") then
    os.execute("systemctl reboot &")
elseif choice:find("Lock") then
    os.execute("hyprlock &")
elseif choice:find("Suspend") then
    os.execute("systemctl suspend &")
elseif choice:find("Log Out") then
    os.execute("hyprctl dispatch exit &")
end
