#!/usr/bin/env bash
# Brightness wrapper for Dynamic Island
# Usage: ./brightness_notify.sh +5% | -5%

VAL="${1:-+5%}"
if command -v dynamic-island >/dev/null 2>&1; then
    dynamic-island brightness "$VAL"
else
    brightnessctl set "$VAL"
fi
