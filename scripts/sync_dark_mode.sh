#!/bin/bash
# Sync dark mode between desktop, GTK, portals, and web browsers (LibreWolf, Firefox, Chrome, etc.)

ACTION="${1:-toggle}"

is_dark() {
    # 1. Check gsettings color-scheme
    local scheme
    scheme=$(gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null | tr -d "'\"")
    if [ "$scheme" = "prefer-dark" ]; then
        return 0
    fi

    # 2. Check gsettings gtk-theme
    local theme
    theme=$(gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null | tr -d "'\"")
    if [[ "$theme" =~ [dD]ark ]]; then
        return 0
    fi

    # 3. Check GTK settings
    if [ -f "$HOME/.config/gtk-3.0/settings.ini" ]; then
        if grep -qi "gtk-application-prefer-dark-theme.*=.*1" "$HOME/.config/gtk-3.0/settings.ini" 2>/dev/null; then
            return 0
        fi
    fi

    # 4. Check browser prefs
    if grep -q 'ui.systemUsesDarkTheme.*1' "$HOME"/.config/librewolf/librewolf/*/user.js "$HOME"/.config/librewolf/librewolf/*/prefs.js "$HOME"/.config/mozilla/firefox/*/user.js "$HOME"/.config/mozilla/firefox/*/prefs.js 2>/dev/null; then
        return 0
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
    CO_VAL=2
    THEME_VAL=2
    THEME_ID="firefox-compact-dark@mozilla.org"
else
    COLOR_SCHEME="default"
    GTK_THEME="Adwaita"
    DARK_NUM=0
    CO_VAL=2
    THEME_VAL=2
    THEME_ID="firefox-compact-light@mozilla.org"
fi

# 1. Update gsettings & dconf
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

# 4. Synchronize Web Browsers (LibreWolf, Firefox, etc.)
python3 -c "
import os, glob, re

target_dark = ($TARGET_DARK == 1)
dark_num = 1 if target_dark else 0
theme_id = '$THEME_ID'

home = os.path.expanduser('~')
profile_patterns = [
    f'{home}/.config/librewolf/librewolf/*',
    f'{home}/.config/mozilla/firefox/*',
    f'{home}/.mozilla/firefox/*'
]

prefs_to_set = [
    ('privacy.resistFingerprinting', 'false'),
    ('privacy.fingerprintingProtection', 'true'),
    ('privacy.fingerprintingProtection.overrides', '\"+AllTargets,-CSSPrefersColorScheme\"'),
    ('layout.css.prefers-color-scheme.content-override', '2'),
    ('browser.theme.content-theme', '2'),
    ('browser.theme.toolbar-theme', '2'),
    ('ui.systemUsesDarkTheme', str(dark_num)),
    ('extensions.activeThemeID', f'\"{theme_id}\"')
]

for pat in profile_patterns:
    for pdir in glob.glob(pat):
        if not os.path.isdir(pdir):
            continue
        
        # 1. Update user.js
        ujs = os.path.join(pdir, 'user.js')
        existing_lines = []
        keys = [p[0] for p in prefs_to_set]
        if os.path.isfile(ujs):
            try:
                with open(ujs, 'r') as f:
                    existing_lines = [
                        line for line in f.readlines()
                        if not any(k in line for k in keys)
                    ]
            except Exception:
                existing_lines = []
        
        for k, v in prefs_to_set:
            existing_lines.append(f'user_pref(\"{k}\", {v});\n')
        
        try:
            with open(ujs, 'w') as f:
                f.writelines(existing_lines)
        except Exception:
            pass

        # Also copy librewolf.overrides.cfg into profile directory
        ocfg = os.path.join(pdir, 'librewolf.overrides.cfg')
        try:
            with open(ocfg, 'w') as f:
                f.write('''defaultPref(\"privacy.resistFingerprinting\", false);
defaultPref(\"privacy.fingerprintingProtection\", true);
defaultPref(\"privacy.fingerprintingProtection.overrides\", \"+AllTargets,-CSSPrefersColorScheme\");
defaultPref(\"layout.css.prefers-color-scheme.content-override\", 2);
defaultPref(\"browser.theme.content-theme\", 2);
defaultPref(\"browser.theme.toolbar-theme\", 2);
defaultPref(\"ui.systemUsesDarkTheme\", ''' + str(dark_num) + ''');\n''')
        except Exception:
            pass

        # 2. Patch live prefs.js if present
        pjs = os.path.join(pdir, 'prefs.js')
        if os.path.isfile(pjs):
            try:
                with open(pjs, 'r') as f:
                    content = f.read()
                for k, v in prefs_to_set:
                    pat_pref = rf'user_pref\(\"{re.escape(k)}\",\s*[^)]+\);'
                    new_pref = f'user_pref(\"{k}\", {v});'
                    if re.search(pat_pref, content):
                        content = re.sub(pat_pref, new_pref, content)
                    else:
                        content += f'\n{new_pref}'
                with open(pjs, 'w') as f:
                    f.write(content)
            except Exception:
                pass
" 2>/dev/null &

wait

if [ "$TARGET_DARK" -eq 1 ]; then
    echo "Dark mode: ON"
else
    echo "Dark mode: OFF"
fi
