#!/usr/bin/env bash
# Dynamic Island Controller Script
# Provides quick actions and testing helpers

BIN="dynamic-island"
if [ -f "./build/dynamic-island" ]; then
    BIN="./build/dynamic-island"
fi

cmd="${1:-toggle}"
shift

case "$cmd" in
    toggle|collapse|reload|quit)
        $BIN "$cmd"
        ;;
    expand)
        $BIN expand "${1:-quick}"
        ;;
    volume)
        $BIN volume "${1:-+5%}"
        ;;
    brightness)
        $BIN brightness "${1:-+5%}"
        ;;
    media)
        $BIN media "${1:-play-pause}"
        ;;
    screenshot)
        $BIN screenshot "${1:-full}"
        ;;
    notify)
        $BIN notify "${1:-Antigravity}" "${2:-Notification test message}"
        ;;
    *)
        echo "Usage: $0 {toggle|collapse|expand <mode>|volume <val>|brightness <val>|media <act>|screenshot <mode>|notify <app> <msg>|reload|quit}"
        exit 1
        ;;
esac
