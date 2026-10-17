#!/usr/bin/env bash
# Sync dark mode between desktop, GTK, portals, and web browsers (LibreWolf, Firefox, Chrome, etc.)
# Highly optimized for performance (pure bash, 0 python overhead)

ACTION="${1:-toggle}"

is_dark() {
    # 1. Primary check: gsettings color-scheme (Freedesktop appearance portal backend)
    local scheme
    scheme=$(gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null | tr -d "'\"")
    if [ "$scheme" = "prefer-dark" ]; then
        return 0
    elif [ "$scheme" = "prefer-light" ]; then
        return 1
    fi

    # 2. Secondary check: gsettings gtk-theme
    local theme
    theme=$(gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null | tr -d "'\"")
    if [[ "$theme" =~ [dD]ark ]]; then
        return 0
    fi

    # 3. Fallback: Check GTK 3.0 settings.ini
    if [ -f "$HOME/.config/gtk-3.0/settings.ini" ]; then
        if grep -qi "gtk-application-prefer-dark-theme.*=.*1" "$HOME/.config/gtk-3.0/settings.ini" 2>/dev/null; then
            return 0
        fi
    fi

    return 1
}

if [ "$ACTION" = "status" ]; then
    if is_dark; then
        echo "dark"
    else
        echo "light"
    fi
    exit 0
fi

TARGET_DARK=0
if [ "$ACTION" = "on" ] || [ "$ACTION" = "true" ] || [ "$ACTION" = "1" ] || [ "$ACTION" = "dark" ]; then
    TARGET_DARK=1
elif [ "$ACTION" = "off" ] || [ "$ACTION" = "false" ] || [ "$ACTION" = "0" ] || [ "$ACTION" = "light" ]; then
    TARGET_DARK=0
elif [ "$ACTION" = "toggle" ]; then
    if is_dark; then
        TARGET_DARK=0
    else
        TARGET_DARK=1
    fi
fi

if [ "$TARGET_DARK" -eq 1 ]; then
    COLOR_SCHEME="prefer-dark"
    GTK_THEME="Adwaita-dark"
    DARK_NUM=1
    THEME_ID="firefox-compact-dark@mozilla.org"
else
    COLOR_SCHEME="prefer-light"
    GTK_THEME="Adwaita"
    DARK_NUM=0
    THEME_ID="firefox-compact-light@mozilla.org"
fi

# 1. Update gsettings & dconf (Triggers Freedesktop Appearance portal signal)
gsettings set org.gnome.desktop.interface color-scheme "$COLOR_SCHEME" 2>/dev/null &
gsettings set org.gnome.desktop.interface gtk-theme "$GTK_THEME" 2>/dev/null &
if command -v dconf >/dev/null 2>&1; then
    dconf write /org/gnome/desktop/interface/color-scheme "'$COLOR_SCHEME'" 2>/dev/null &
    dconf write /org/gnome/desktop/interface/gtk-theme "'$GTK_THEME'" 2>/dev/null &
fi

# 2. Update GTK-3.0 and GTK-4.0 settings.ini
mkdir -p "$HOME/.config/gtk-3.0" "$HOME/.config/gtk-4.0"
for sfile in "$HOME/.config/gtk-3.0/settings.ini" "$HOME/.config/gtk-4.0/settings.ini"; do
    if [ ! -f "$sfile" ]; then
        cat <<EOF > "$sfile"
[Settings]
gtk-application-prefer-dark-theme=$DARK_NUM
gtk-theme-name=$GTK_THEME
EOF
    else
        if grep -q "gtk-application-prefer-dark-theme" "$sfile"; then
            sed -i "s/gtk-application-prefer-dark-theme=.*/gtk-application-prefer-dark-theme=$DARK_NUM/" "$sfile"
        else
            echo "gtk-application-prefer-dark-theme=$DARK_NUM" >> "$sfile"
        fi
        if grep -q "gtk-theme-name" "$sfile"; then
            sed -i "s/gtk-theme-name=.*/gtk-theme-name=$GTK_THEME/" "$sfile"
        else
            echo "gtk-theme-name=$GTK_THEME" >> "$sfile"
        fi
    fi
done

# 3. Create LibreWolf overrides config
mkdir -p "$HOME/.librewolf" "$HOME/.config/librewolf/librewolf"
for ocfg in "$HOME/.librewolf/librewolf.overrides.cfg" "$HOME/.config/librewolf/librewolf/librewolf.overrides.cfg"; do
    cat <<EOF > "$ocfg"
// LibreWolf Overrides for Theme & Dark Mode Sync with OS / Dynamic Island
// Disable strict RFP spoofing of color scheme, keep FPP fingerprint protection active
defaultPref("privacy.resistFingerprinting", false);
defaultPref("privacy.fingerprintingProtection", true);
defaultPref("privacy.fingerprintingProtection.overrides", "+AllTargets,-CSSPrefersColorScheme");

// Follow system color scheme for websites and browser chrome
defaultPref("layout.css.prefers-color-scheme.content-override", 2);
defaultPref("browser.theme.content-theme", 2);
defaultPref("browser.theme.toolbar-theme", 2);
defaultPref("ui.systemUsesDarkTheme", $DARK_NUM);
EOF
done

# 4. Synchronize Web Browsers in pure bash without python process overhead
for pdir in "$HOME/.config/librewolf/librewolf"/* "$HOME/.config/mozilla/firefox"/* "$HOME/.mozilla/firefox"/*; do
    [ -d "$pdir" ] || continue
    ujs="$pdir/user.js"
    if [ -f "$ujs" ] || [ -f "$pdir/prefs.js" ]; then
        tmp_ujs="$pdir/user.js.tmp.$$"
        if [ -f "$ujs" ]; then
            grep -v -E "privacy\.resistFingerprinting|privacy\.fingerprintingProtection|layout\.css\.prefers-color-scheme|browser\.theme|ui\.systemUsesDarkTheme|extensions\.activeThemeID" "$ujs" > "$tmp_ujs" 2>/dev/null || true
        else
            touch "$tmp_ujs"
        fi
        cat <<EOF >> "$tmp_ujs"
user_pref("privacy.resistFingerprinting", false);
user_pref("privacy.fingerprintingProtection", true);
user_pref("privacy.fingerprintingProtection.overrides", "+AllTargets,-CSSPrefersColorScheme");
user_pref("layout.css.prefers-color-scheme.content-override", 2);
user_pref("browser.theme.content-theme", 2);
user_pref("browser.theme.toolbar-theme", 2);
user_pref("ui.systemUsesDarkTheme", $DARK_NUM);
user_pref("extensions.activeThemeID", "$THEME_ID");
EOF
        mv -f "$tmp_ujs" "$ujs"

        # Also copy librewolf.overrides.cfg into profile directory if librewolf profile
        if [[ "$pdir" == *"librewolf"* ]]; then
            cp -f "$HOME/.config/librewolf/librewolf/librewolf.overrides.cfg" "$pdir/librewolf.overrides.cfg" 2>/dev/null || true
        fi
    fi
done

wait

if [ "$TARGET_DARK" -eq 1 ]; then
    echo "Dark mode: ON"
else
    echo "Dark mode: OFF"
fi
