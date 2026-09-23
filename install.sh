#!/usr/bin/env bash
# ==============================================================================
# Dynamic Island Installer for CachyOS / Arch Linux + Hyprland
# ==============================================================================

set -e

COLOR_RESET="\033[0m"
COLOR_GREEN="\033[1;32m"
COLOR_BLUE="\033[1;34m"
COLOR_YELLOW="\033[1;33m"
COLOR_RED="\033[1;31m"
COLOR_CYAN="\033[1;36m"

echo -e "${COLOR_CYAN}======================================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}      Dynamic Island Installer (Hyprland / Wayland)   ${COLOR_RESET}"
echo -e "${COLOR_CYAN}======================================================${COLOR_RESET}"
echo ""

# 1. Check Wayland
if [ -z "$WAYLAND_DISPLAY" ]; then
    echo -e "${COLOR_YELLOW}[WARNING] WAYLAND_DISPLAY environment variable is not set.${COLOR_RESET}"
    echo -e "Make sure you are logged in to a Wayland session."
else
    echo -e "${COLOR_GREEN}[OK] Wayland display detected:${COLOR_RESET} $WAYLAND_DISPLAY"
fi

# 2. Check Hyprland
if command -v hyprctl >/dev/null 2>&1; then
    echo -e "${COLOR_GREEN}[OK] Hyprland detected:${COLOR_RESET} $(hyprctl version 2>/dev/null | head -n 1 || echo 'Hyprland')"
else
    echo -e "${COLOR_YELLOW}[WARNING] hyprctl not found. Dynamic Island is optimized specifically for Hyprland.${COLOR_RESET}"
fi

# 3. Check Required Build Dependencies
echo -e "\n${COLOR_BLUE}Checking build dependencies...${COLOR_RESET}"
MISSING_PKGS=()

check_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        MISSING_PKGS+=("$2")
    fi
}

check_pkg_config() {
    if ! pkg-config --exists "$1" >/dev/null 2>&1; then
        MISSING_PKGS+=("$2")
    fi
}

check_cmd gcc "gcc"
check_cmd g++ "gcc"
check_cmd make "make"
check_cmd pkg-config "pkgconf"

check_pkg_config "wayland-client" "wayland"
check_pkg_config "cairo" "cairo"
check_pkg_config "pango" "pango"
check_pkg_config "pangocairo" "pango"

# Runtime utilities
check_cmd playerctl "playerctl"
check_cmd wpctl "wireplumber"
check_cmd grim "grim"
check_cmd slurp "slurp"
check_cmd wl-copy "wl-clipboard"
check_cmd brightnessctl "brightnessctl"

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    # Deduplicate
    UNIQUE_PKGS=($(printf "%s\n" "${MISSING_PKGS[@]}" | sort -u))
    echo -e "${COLOR_YELLOW}[INFO] Missing packages detected:${COLOR_RESET} ${UNIQUE_PKGS[*]}"
    echo -e "Would you like to install them now with pacman/paru? (Requires sudo password)"
    read -p "Install dependencies? [y/N]: " ans
    if [[ "$ans" =~ ^[Yy]$ ]]; then
        if command -v paru >/dev/null 2>&1; then
            paru -S --needed --noconfirm "${UNIQUE_PKGS[@]}"
        elif command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed --noconfirm "${UNIQUE_PKGS[@]}"
        else
            echo -e "${COLOR_RED}Please install:${COLOR_RESET} ${UNIQUE_PKGS[*]}"
            exit 1
        fi
    else
        echo -e "${COLOR_YELLOW}Continuing without installing missing packages. Compilation might fail.${COLOR_RESET}"
    fi
else
    echo -e "${COLOR_GREEN}[OK] All dependencies are satisfied!${COLOR_RESET}"
fi

# 4. Compile Dynamic Island
echo -e "\n${COLOR_BLUE}Building Dynamic Island...${COLOR_RESET}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

make clean
make -j"$(nproc)"

if [ ! -f "build/dynamic-island" ]; then
    echo -e "${COLOR_RED}[ERROR] Build failed! Check compiler logs above.${COLOR_RESET}"
    exit 1
fi
echo -e "${COLOR_GREEN}[OK] Build successful! Binary size: $(ls -lh build/dynamic-island | awk '{print $5}')${COLOR_RESET}"

# 5. Install Binary
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"
cp -f build/dynamic-island "$INSTALL_DIR/dynamic-island"
chmod +x "$INSTALL_DIR/dynamic-island"
echo -e "${COLOR_GREEN}[OK] Installed binary to:${COLOR_RESET} $INSTALL_DIR/dynamic-island"

# Check PATH
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo -e "${COLOR_YELLOW}[NOTE] $HOME/.local/bin is not in your current PATH.${COLOR_RESET}"
    echo -e "Consider adding: 'export PATH=\"\$HOME/.local/bin:\$PATH\"' to ~/.config/fish/config.fish or ~/.bashrc"
fi

# 6. Install Configuration & Themes
CONFIG_DIR="$HOME/.config/dynamic-island"
mkdir -p "$CONFIG_DIR"
mkdir -p "$CONFIG_DIR/themes"

if [ ! -f "$CONFIG_DIR/config.json" ]; then
    cp -f config/config.json "$CONFIG_DIR/config.json"
    echo -e "${COLOR_GREEN}[OK] Created configuration:${COLOR_RESET} $CONFIG_DIR/config.json"
else
    echo -e "${COLOR_BLUE}[INFO] Preserved existing config:${COLOR_RESET} $CONFIG_DIR/config.json"
fi

cp -rf themes/* "$CONFIG_DIR/themes/"
echo -e "${COLOR_GREEN}[OK] Installed themes to:${COLOR_RESET} $CONFIG_DIR/themes/"

# 7. Configure Hyprland Autostart and Keybindings
HYPR_DIR="$HOME/.config/hypr"
HYPR_CONF="$HYPR_DIR/hyprland.conf"
DI_HYPR_CONF="$HYPR_DIR/dynamic-island.conf"

mkdir -p "$HYPR_DIR"
cp -f hyprland/dynamic-island.conf "$DI_HYPR_CONF"
echo -e "${COLOR_GREEN}[OK] Installed Hyprland config snippet:${COLOR_RESET} $DI_HYPR_CONF"

if [ -f "$HYPR_CONF" ]; then
    if grep -q "dynamic-island" "$HYPR_CONF"; then
        echo -e "${COLOR_BLUE}[INFO] Dynamic Island already configured in:${COLOR_RESET} $HYPR_CONF"
    else
        # Backup hyprland.conf
        BACKUP="$HYPR_CONF.backup_$(date +%Y%m%d_%H%M%S)"
        cp "$HYPR_CONF" "$BACKUP"
        echo -e "${COLOR_GREEN}[OK] Backup created:${COLOR_RESET} $BACKUP"

        # Append source line
        echo "" >> "$HYPR_CONF"
        echo "# Dynamic Island Integration" >> "$HYPR_CONF"
        echo "source = ~/.config/hypr/dynamic-island.conf" >> "$HYPR_CONF"
        echo -e "${COLOR_GREEN}[OK] Added 'source = ~/.config/hypr/dynamic-island.conf' to:${COLOR_RESET} $HYPR_CONF"
    fi
else
    echo -e "${COLOR_YELLOW}[NOTE] $HYPR_CONF not found. Created template with dynamic-island.conf source.${COLOR_RESET}"
    echo "source = ~/.config/hypr/dynamic-island.conf" > "$HYPR_CONF"
fi

# 8. Systemd User Service (Optional)
SYSTEMD_USER_DIR="$HOME/.config/systemd/user"
mkdir -p "$SYSTEMD_USER_DIR"
cat << EOF > "$SYSTEMD_USER_DIR/dynamic-island.service"
[Unit]
Description=Dynamic Island Topbar for Hyprland
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=simple
ExecStart=$HOME/.local/bin/dynamic-island
Restart=on-failure
RestartSec=1s

[Install]
WantedBy=graphical-session.target
EOF
echo -e "${COLOR_GREEN}[OK] Created systemd user unit:${COLOR_RESET} $SYSTEMD_USER_DIR/dynamic-island.service"

echo ""
echo -e "${COLOR_GREEN}======================================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}       Dynamic Island Installation Completed!        ${COLOR_RESET}"
echo -e "${COLOR_GREEN}======================================================${COLOR_RESET}"
echo ""
echo -e "To start Dynamic Island now, run:"
echo -e "  ${COLOR_CYAN}dynamic-island &${COLOR_RESET}"
echo ""
echo -e "Keybindings configured:"
echo -e "  • ${COLOR_YELLOW}SUPER + I${COLOR_RESET}       : Toggle Dynamic Island expand / collapse"
echo -e "  • ${COLOR_YELLOW}SUPER + V${COLOR_RESET}       : Expand Audio volume control"
echo -e "  • ${COLOR_YELLOW}SUPER + M${COLOR_RESET}       : Expand Media player"
echo -e "  • ${COLOR_YELLOW}SUPER + S${COLOR_RESET}       : Expand System monitor (CPU/RAM/GPU)"
echo -e "  • ${COLOR_YELLOW}SUPER + C${COLOR_RESET}       : Expand Clock & Calendar"
echo -e "  • ${COLOR_YELLOW}Print${COLOR_RESET}           : Take fullscreen screenshot"
echo -e "  • ${COLOR_YELLOW}Shift + Print${COLOR_RESET}   : Take area screenshot"
echo -e "  • ${COLOR_YELLOW}Right Click${COLOR_RESET}     : Open Quick Actions"
echo -e "  • ${COLOR_YELLOW}Scroll Wheel${COLOR_RESET}    : Adjust volume"
echo ""
