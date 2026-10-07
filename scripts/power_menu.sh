#!/usr/bin/env bash
# ==============================================================================
# Dynamic Island Power Menu for Hyprland
# Integrates with wlogout, rofi, wofi, and systemctl
# ==============================================================================

if command -v wlogout >/dev/null 2>&1; then
    wlogout -b 2 &
    exit 0
fi

OPTIONS="󰐥  Shutdown\n󰜉  Restart\n󰌾  Lock Screen\n󰤄  Suspend\n󰗽  Log Out"

if command -v rofi >/dev/null 2>&1; then
    CHOICE=$(echo -e "$OPTIONS" | rofi -dmenu -i -p "Power Menu" -theme-str 'window {width: 280px; border-radius: 12px;} listview {lines: 5;}')
elif command -v wofi >/dev/null 2>&1; then
    CHOICE=$(echo -e "$OPTIONS" | wofi --dmenu --prompt "Power Menu" --width 280 --lines 5)
else
    # Direct fallback: systemctl poweroff
    systemctl poweroff
    exit 0
fi

case "$CHOICE" in
    *"Shutdown"*)
        systemctl poweroff
        ;;
    *"Restart"*)
        systemctl reboot
        ;;
    *"Lock Screen"*)
        hyprlock &
        ;;
    *"Suspend"*)
        systemctl suspend
        ;;
    *"Log Out"*)
        hyprctl dispatch exit
        ;;
esac
