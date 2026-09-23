#!/usr/bin/env bash
# ==============================================================================
# Dynamic Island Uninstaller
# ==============================================================================

set -e

COLOR_RESET="\033[0m"
COLOR_GREEN="\033[1;32m"
COLOR_BLUE="\033[1;34m"
COLOR_YELLOW="\033[1;33m"
COLOR_RED="\033[1;31m"

echo -e "${COLOR_YELLOW}======================================================${COLOR_RESET}"
echo -e "${COLOR_YELLOW}            Dynamic Island Uninstaller                ${COLOR_RESET}"
echo -e "${COLOR_YELLOW}======================================================${COLOR_RESET}"
echo ""

# 1. Stop running daemon
echo -e "${COLOR_BLUE}Stopping running dynamic-island processes...${COLOR_RESET}"
if command -v dynamic-island >/dev/null 2>&1; then
    dynamic-island quit 2>/dev/null || true
fi
pkill -f "dynamic-island" 2>/dev/null || true
echo -e "${COLOR_GREEN}[OK] Process stopped.${COLOR_RESET}"

# 2. Disable systemd user service if present
if systemctl --user is-enabled dynamic-island.service >/dev/null 2>&1; then
    systemctl --user stop dynamic-island.service 2>/dev/null || true
    systemctl --user disable dynamic-island.service 2>/dev/null || true
    echo -e "${COLOR_GREEN}[OK] Disabled systemd user service.${COLOR_RESET}"
fi
rm -f "$HOME/.config/systemd/user/dynamic-island.service"

# 3. Remove binary
BIN_PATH="$HOME/.local/bin/dynamic-island"
if [ -f "$BIN_PATH" ]; then
    rm -f "$BIN_PATH"
    echo -e "${COLOR_GREEN}[OK] Removed binary:${COLOR_RESET} $BIN_PATH"
fi

# 4. Remove Hyprland configuration snippet
HYPR_CONF="$HOME/.config/hypr/hyprland.conf"
DI_HYPR_CONF="$HOME/.config/hypr/dynamic-island.conf"

if [ -f "$DI_HYPR_CONF" ]; then
    rm -f "$DI_HYPR_CONF"
    echo -e "${COLOR_GREEN}[OK] Removed snippet:${COLOR_RESET} $DI_HYPR_CONF"
fi

if [ -f "$HYPR_CONF" ]; then
    # Backup before altering
    cp "$HYPR_CONF" "$HYPR_CONF.backup_uninstall_$(date +%Y%m%d_%H%M%S)"
    sed -i '/source.*dynamic-island\.conf/d' "$HYPR_CONF"
    sed -i '/# Dynamic Island Integration/d' "$HYPR_CONF"
    echo -e "${COLOR_GREEN}[OK] Cleaned references in:${COLOR_RESET} $HYPR_CONF"
fi

# 5. Clean configuration directory
CONFIG_DIR="$HOME/.config/dynamic-island"
if [ -d "$CONFIG_DIR" ]; then
    read -p "Remove user configuration directory (~/.config/dynamic-island)? [y/N]: " ans
    if [[ "$ans" =~ ^[Yy]$ ]]; then
        rm -rf "$CONFIG_DIR"
        echo -e "${COLOR_GREEN}[OK] Removed config directory:${COLOR_RESET} $CONFIG_DIR"
    else
        echo -e "${COLOR_BLUE}[INFO] Preserved config directory:${COLOR_RESET} $CONFIG_DIR"
    fi
fi

echo ""
echo -e "${COLOR_GREEN}======================================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}       Dynamic Island uninstalled successfully!       ${COLOR_RESET}"
echo -e "${COLOR_GREEN}======================================================${COLOR_RESET}"
