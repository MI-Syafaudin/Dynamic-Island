#!/usr/bin/env bash
# Volume wrapper for Dynamic Island
# Usage: ./volume_notify.sh +5% | -5% | toggle

VAL="${1:-+5%}"
if command -v dynamic-island >/dev/null 2>&1; then
    dynamic-island volume "$VAL"
else
    if [ "$VAL" = "toggle" ]; then
        wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle
    else
        wpctl set-volume @DEFAULT_AUDIO_SINK@ "$VAL"
    fi
fi
