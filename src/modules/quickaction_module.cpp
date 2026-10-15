#include "quickaction_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <unistd.h>

namespace di {

QuickActionModule::QuickActionModule() {
    update_async();
}

QuickActionModule::~QuickActionModule() {
    std::lock_guard<std::mutex> lock(m_worker_mutex);
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}

void QuickActionModule::update() {
    update_async();
}

void QuickActionModule::update_async() {
    bool expected = false;
    if (!m_updating.compare_exchange_strong(expected, true)) {
        return; // Query is already in flight
    }

    std::lock_guard<std::mutex> lock(m_worker_mutex);
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_worker_thread = std::thread([this]() {
        query_states();
        m_updating.store(false);
    });
}

void QuickActionModule::query_states() {
    // 1. WiFi state: Check sysfs rfkill first (instantaneous microsecond read)
    bool wifi_on = false;
    bool found_rfkill_wlan = false;
    for (int i = 0; i < 16; ++i) {
        std::string path_type = "/sys/class/rfkill/rfkill" + std::to_string(i) + "/type";
        std::string path_soft = "/sys/class/rfkill/rfkill" + std::to_string(i) + "/soft";
        FILE* tf = fopen(path_type.c_str(), "r");
        if (tf) {
            char tbuf[32];
            if (fgets(tbuf, sizeof(tbuf), tf)) {
                if (std::string(tbuf).find("wlan") != std::string::npos) {
                    found_rfkill_wlan = true;
                    FILE* sf = fopen(path_soft.c_str(), "r");
                    if (sf) {
                        char sbuf[16];
                        if (fgets(sbuf, sizeof(sbuf), sf) && sbuf[0] == '0') {
                            wifi_on = true;
                        }
                        fclose(sf);
                    }
                }
            }
            fclose(tf);
        }
    }
    if (!found_rfkill_wlan) {
        FILE* fp = popen("nmcli radio wifi 2>/dev/null", "r");
        if (fp) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp)) {
                std::string out = buf;
                wifi_on = (out.find("enabled") != std::string::npos);
            }
            pclose(fp);
        }
    }
    m_wifi_active.store(wifi_on);

    // 2. Bluetooth state: Check sysfs rfkill first (instantaneous microsecond read)
    bool bt_on = false;
    bool found_rfkill_bt = false;
    for (int i = 0; i < 16; ++i) {
        std::string path_type = "/sys/class/rfkill/rfkill" + std::to_string(i) + "/type";
        std::string path_soft = "/sys/class/rfkill/rfkill" + std::to_string(i) + "/soft";
        FILE* tf = fopen(path_type.c_str(), "r");
        if (tf) {
            char tbuf[32];
            if (fgets(tbuf, sizeof(tbuf), tf)) {
                if (std::string(tbuf).find("bluetooth") != std::string::npos) {
                    found_rfkill_bt = true;
                    FILE* sf = fopen(path_soft.c_str(), "r");
                    if (sf) {
                        char sbuf[16];
                        if (fgets(sbuf, sizeof(sbuf), sf) && sbuf[0] == '0') {
                            bt_on = true;
                        }
                        fclose(sf);
                    }
                }
            }
            fclose(tf);
        }
    }
    if (!found_rfkill_bt) {
        FILE* fp = popen("bluetoothctl show 2>/dev/null", "r");
        if (fp) {
            char buf[256];
            while (fgets(buf, sizeof(buf), fp)) {
                std::string line = buf;
                if (line.find("Powered: yes") != std::string::npos) {
                    bt_on = true;
                    break;
                } else if (line.find("Powered: no") != std::string::npos) {
                    bt_on = false;
                    break;
                }
            }
            pclose(fp);
        }
    }
    m_bt_active.store(bt_on);

    // 3. Audio Mute state: wpctl get-volume @DEFAULT_AUDIO_SINK@
    FILE* fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    bool mute_on = false;
    if (fp) {
        char buf[128];
        std::string out;
        while (fgets(buf, sizeof(buf), fp)) out += buf;
        pclose(fp);
        mute_on = (out.find("[MUTED]") != std::string::npos);
    }
    m_mute_active.store(mute_on);

    // 4. Dark mode state: Check gsettings, gtk-theme, GTK settings, and browser preferences
    bool dark_on = false;
    FILE* fp_dark = popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (fp_dark) {
        char dbuf[128];
        std::string dout;
        while (fgets(dbuf, sizeof(dbuf), fp_dark)) dout += dbuf;
        pclose(fp_dark);
        if (dout.find("prefer-dark") != std::string::npos) {
            dark_on = true;
        }
    }
    if (!dark_on) {
        FILE* fp_gtk = popen("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", "r");
        if (fp_gtk) {
            char gbuf[128];
            std::string gout;
            while (fgets(gbuf, sizeof(gbuf), fp_gtk)) gout += gbuf;
            pclose(fp_gtk);
            if (gout.find("dark") != std::string::npos || gout.find("Dark") != std::string::npos) {
                dark_on = true;
            }
        }
    }
    if (!dark_on) {
        FILE* fp_br = popen("grep -q 'layout.css.prefers-color-scheme.content-override.*0' $HOME/.config/librewolf/librewolf/*/user.js $HOME/.config/librewolf/librewolf/*/prefs.js $HOME/.config/mozilla/firefox/*/user.js $HOME/.config/mozilla/firefox/*/prefs.js 2>/dev/null && echo dark", "r");
        if (fp_br) {
            char bbuf[64];
            if (fgets(bbuf, sizeof(bbuf), fp_br)) {
                if (std::string(bbuf).find("dark") != std::string::npos) {
                    dark_on = true;
                }
            }
            pclose(fp_br);
        }
    }
    m_dark_active.store(dark_on);

    // 5. Night light state: check processes (hyprsunset, wlsunset, gammastep) or Hyprland screen_shader
    bool proc_running = (std::system("pgrep -x hyprsunset >/dev/null 2>&1 || pgrep -x wlsunset >/dev/null 2>&1 || pgrep -x gammastep >/dev/null 2>&1") == 0);
    bool shader_active = false;
    if (!proc_running) {
        FILE* fp_sh = popen("hyprctl getoption decoration:screen_shader -j 2>/dev/null", "r");
        if (fp_sh) {
            char sbuf[256];
            std::string sout;
            while (fgets(sbuf, sizeof(sbuf), fp_sh)) sout += sbuf;
            pclose(fp_sh);
            size_t str_pos = sout.find("\"str\":");
            if (str_pos != std::string::npos) {
                size_t q1 = sout.find('"', str_pos + 6);
                size_t q2 = (q1 != std::string::npos) ? sout.find('"', q1 + 1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                    std::string val = sout.substr(q1 + 1, q2 - q1 - 1);
                    if (!val.empty() && val != "[[EMPTY]]") {
                        shader_active = true;
                    }
                }
            }
        }
    }
    m_night_active.store(proc_running || shader_active);
}

void QuickActionModule::get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr) {
    (void)font_desc;
    (void)config;
    (void)cr;
    w = 540.0;
    h = 76.0;
}

void QuickActionModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    // Subtle menu / controls icon at the right edge
    std::string icon = "󰍜";
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, icon.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.6);
    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "quick"});
    current_x += tw + 6;

    g_object_unref(layout);
}

void QuickActionModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    double target_h = 76.0;
    double idle_h = static_cast<double>(config.idle_height);

    // Calculate expansion progress [0.0 .. 1.0]
    double progress = (target_h > idle_h) 
        ? std::clamp((static_cast<double>(h) - idle_h) / (target_h - idle_h), 0.0, 1.0)
        : 1.0;

    // Suppress drawing early in the expansion to avoid severe clipping and artifacting
    if (progress < 0.25 || w < 200) {
        return;
    }

    // Smoothstep fade-in alpha
    double t = (progress - 0.25) / 0.75;
    t = std::clamp(t, 0.0, 1.0);
    double alpha = t * t * (3.0 - 2.0 * t);

    // Subtle upward glide as it reveals (micro-animation)
    int anim_offset_y = static_cast<int>((1.0 - progress) * 6.0);

    // Group buffering for clean, high-performance alpha blend
    cairo_push_group(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Header centered
    pango_layout_set_text(layout, "Quick Controls", -1);
    int htw, hth;
    pango_layout_get_pixel_size(layout, &htw, &hth);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, (w - htw) / 2, 10 + anim_offset_y / 2);
    pango_cairo_show_layout(cr, layout);

    // Row of 7 buttons: WiFi, Bluetooth, Mute, Dark, Eye Comfort, Screen Shot, Power
    struct ActionBtn {
        std::string icon;
        std::string label;
        std::string action;
        bool is_active;
        ColorRGBA active_color;
    };

    bool wifi = m_wifi_active.load();
    bool bt = m_bt_active.load();
    bool mute = m_mute_active.load();
    bool dark = m_dark_active.load();
    bool eye = m_night_active.load();

    std::vector<ActionBtn> buttons = {
        {wifi ? "󰖩" : "󰖪", "WiFi", "qa_wifi", wifi, config.colors.accent_blue},
        {bt ? "󰂯" : "󰂲", "Bluetooth", "qa_bt", bt, config.colors.accent_blue},
        {mute ? "󰖁" : "󰕾", mute ? "Muted" : "Mute", "qa_mute", mute, config.colors.danger},
        {dark ? "󰔎" : "󰃭", "Dark", "qa_dark", dark, config.colors.accent_purple},
        {eye ? "󰈈" : "󰈉", "Eye Comfort", "qa_night", eye, config.colors.warning},
        {"󰹑", "Screen Shot", "qa_shot", false, config.colors.accent_blue},
        {"󰐥", "Power", "qa_power", false, config.colors.danger}
    };

    int btn_w = 68;
    int btn_h = 36;
    int btn_y = 30 + anim_offset_y;
    int gap = 6;
    int total_btns_w = buttons.size() * btn_w + (buttons.size() - 1) * gap;
    int start_x = (w - total_btns_w) / 2;

    for (size_t i = 0; i < buttons.size(); ++i) {
        int bx = start_x + i * (btn_w + gap);

        cairo_new_sub_path(cr);
        double r = 8.0;
        cairo_arc(cr, bx + r, btn_y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + btn_w - r, btn_y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, bx + btn_w - r, btn_y + btn_h - r, r, 0, M_PI / 2);
        cairo_arc(cr, bx + r, btn_y + btn_h - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);

        if (buttons[i].is_active) {
            // Active background
            cairo_set_source_rgba(cr, buttons[i].active_color.r, buttons[i].active_color.g, buttons[i].active_color.b, 0.95);
            cairo_fill_preserve(cr);
            // Subtle highlight border
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            // Active indicator dot at top-right
            cairo_new_sub_path(cr);
            cairo_arc(cr, bx + btn_w - 7, btn_y + 7, 2.5, 0, 2 * M_PI);
            if (buttons[i].action == "qa_night" || buttons[i].action == "qa_eye") {
                cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 1.0);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            }
            cairo_fill(cr);
        } else {
            // Inactive background
            cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.9);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.28, 0.28, 0.32, 0.4);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        // Icon
        pango_layout_set_font_description(layout, font_desc);
        pango_layout_set_text(layout, buttons[i].icon.c_str(), -1);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (buttons[i].is_active) {
            if (buttons[i].action == "qa_night" || buttons[i].action == "qa_eye") {
                cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 1.0);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            }
        } else {
            cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.75);
        }
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 4);
        pango_cairo_show_layout(cr, layout);

        // Label with dynamic auto-fit (cached to avoid re-measuring on every frame)
        PangoFontDescription* label_desc = pango_font_description_copy(font_desc);
        auto it = m_label_font_sizes.find(buttons[i].label);
        double cur_sz = 6.2;
        if (it != m_label_font_sizes.end()) {
            cur_sz = it->second;
            pango_font_description_set_size(label_desc, static_cast<int>(cur_sz * PANGO_SCALE));
            pango_layout_set_font_description(layout, label_desc);
            pango_layout_set_text(layout, buttons[i].label.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
        } else {
            pango_font_description_set_size(label_desc, static_cast<int>(cur_sz * PANGO_SCALE));
            pango_layout_set_font_description(layout, label_desc);
            pango_layout_set_text(layout, buttons[i].label.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);

            while (tw > btn_w - 6 && cur_sz > 4.5) {
                cur_sz -= 0.3;
                pango_font_description_set_size(label_desc, static_cast<int>(cur_sz * PANGO_SCALE));
                pango_layout_set_font_description(layout, label_desc);
                pango_layout_get_pixel_size(layout, &tw, &th);
            }
            m_label_font_sizes[buttons[i].label] = cur_sz;
        }

        if (buttons[i].is_active) {
            if (buttons[i].action == "qa_night" || buttons[i].action == "qa_eye") {
                cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.9);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
            }
        } else {
            cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.7);
        }
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 20);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(label_desc);

        // Only register hitboxes when island is almost fully expanded
        if (progress >= 0.85) {
            hitboxes.push_back({bx, btn_y, btn_w, btn_h, buttons[i].action, ""});
        }
    }

    g_object_unref(layout);

    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}

void QuickActionModule::set_dark_mode(bool enable) {
    m_dark_active.store(enable);

    // Call sync_dark_mode.sh helper script or fallback commands
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";
    std::string script1 = home_str + "/.config/dynamic-island/scripts/sync_dark_mode.sh";
    std::string script2 = home_str + "/Projects/dynamic_island/scripts/sync_dark_mode.sh";
    std::string script3 = "/usr/local/bin/dynamic-island-sync-dark";
    std::string script4 = home_str + "/.local/bin/dynamic-island-sync-dark";

    std::string arg = enable ? "on" : "off";

    std::string found_script = "";
    if (access(script1.c_str(), X_OK) == 0) found_script = script1;
    else if (access(script2.c_str(), X_OK) == 0) found_script = script2;
    else if (access(script3.c_str(), X_OK) == 0) found_script = script3;
    else if (access(script4.c_str(), X_OK) == 0) found_script = script4;

    if (!found_script.empty()) {
        std::system(("\"" + found_script + "\" " + arg + " >/dev/null 2>&1 &").c_str());
    } else {
        std::string scheme = enable ? "prefer-dark" : "default";
        std::string gtk_theme = enable ? "Adwaita-dark" : "Adwaita";
        int dark_num = enable ? 1 : 0;

        std::string cmd = 
            "gsettings set org.gnome.desktop.interface color-scheme '" + scheme + "' & "
            "gsettings set org.gnome.desktop.interface gtk-theme '" + gtk_theme + "' & "
            "which dconf >/dev/null 2>&1 && dconf write /org/gnome/desktop/interface/color-scheme \"'" + scheme + "'\" & "
            "mkdir -p ~/.config/gtk-3.0 ~/.config/gtk-4.0 && "
            "for sfile in ~/.config/gtk-3.0/settings.ini ~/.config/gtk-4.0/settings.ini; do "
            "  echo -e \"[Settings]\\ngtk-application-prefer-dark-theme=" + std::to_string(dark_num) + "\\ngtk-theme-name=" + gtk_theme + "\" > \"$sfile\"; "
            "done & "
            "mkdir -p ~/.librewolf ~/.config/librewolf/librewolf && "
            "for ocfg in ~/.librewolf/librewolf.overrides.cfg ~/.config/librewolf/librewolf/librewolf.overrides.cfg; do "
            "  echo -e '// LibreWolf Overrides\\ndefaultPref(\"privacy.resistFingerprinting\", false);\\ndefaultPref(\"privacy.fingerprintingProtection\", true);\\ndefaultPref(\"privacy.fingerprintingProtection.overrides\", \"+AllTargets,-CSSPrefersColorScheme\");\\ndefaultPref(\"layout.css.prefers-color-scheme.content-override\", 2);\\ndefaultPref(\"browser.theme.content-theme\", 2);\\ndefaultPref(\"browser.theme.toolbar-theme\", 2);\\ndefaultPref(\"ui.systemUsesDarkTheme\", " + std::to_string(dark_num) + ");' > \"$ocfg\"; "
            "done & "
            "python3 -c \""
            "import os, glob\n"
            "home = os.path.expanduser('~')\n"
            "for pat in [f'{home}/.config/librewolf/librewolf/*', f'{home}/.config/mozilla/firefox/*']:\n"
            "  for pdir in glob.glob(pat):\n"
            "    if os.path.isdir(pdir):\n"
            "      ujs = os.path.join(pdir, 'user.js')\n"
            "      lines = [l for l in open(ujs).readlines() if not any(k in l for k in ['content-override', 'content-theme', 'toolbar-theme', 'systemUsesDarkTheme', 'resistFingerprinting', 'fingerprintingProtection'])] if os.path.isfile(ujs) else []\n"
            "      lines.append('user_pref(\\\"privacy.resistFingerprinting\\\", false);\\\\n')\n"
            "      lines.append('user_pref(\\\"privacy.fingerprintingProtection\\\", true);\\\\n')\n"
            "      lines.append('user_pref(\\\"privacy.fingerprintingProtection.overrides\\\", \\\"+AllTargets,-CSSPrefersColorScheme\\\");\\\\n')\n"
            "      lines.append('user_pref(\\\"layout.css.prefers-color-scheme.content-override\\\", 2);\\\\n')\n"
            "      lines.append('user_pref(\\\"browser.theme.content-theme\\\", 2);\\\\n')\n"
            "      lines.append(f'user_pref(\\\"ui.systemUsesDarkTheme\\\", " + std::to_string(dark_num) + ");\\\\n')\n"
            "      try: open(ujs, 'w').writelines(lines)\n"
            "      except: pass\n"
            "\" >/dev/null 2>&1 &";
        std::system(cmd.c_str());
    }
}

bool QuickActionModule::handle_click(const std::string& action, const std::string& param) {
    (void)param;
    if (action == "qa_wifi") {
        bool new_val = !m_wifi_active.load();
        m_wifi_active.store(new_val);
        if (new_val) {
            std::system("nmcli radio wifi on &");
        } else {
            std::system("nmcli radio wifi off &");
        }
        return true;
    }
    if (action == "qa_bt") {
        bool new_val = !m_bt_active.load();
        m_bt_active.store(new_val);
        if (new_val) {
            std::system("bluetoothctl power on &");
        } else {
            std::system("bluetoothctl power off &");
        }
        return true;
    }
    if (action == "qa_mute") {
        bool new_val = !m_mute_active.load();
        m_mute_active.store(new_val);
        std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle &");
        return true;
    }
    if (action == "qa_dark") {
        set_dark_mode(!m_dark_active.load());
        return true;
    }
    if (action == "qa_night" || action == "qa_eye" || action == "qa_eye_comfort") {
        bool new_val = !m_night_active.load();
        m_night_active.store(new_val);
        if (new_val) {
            std::system("if command -v hyprsunset >/dev/null 2>&1; then hyprsunset --temperature 4500 & "
                        "elif command -v wlsunset >/dev/null 2>&1; then wlsunset -t 4500 & "
                        "elif command -v gammastep >/dev/null 2>&1; then gammastep -O 4500 & "
                        "else "
                        "  SHADER=\"$HOME/.config/dynamic-island/shaders/nightmode.frag\"; "
                        "  [ ! -f \"$SHADER\" ] && SHADER=\"$HOME/Projects/dynamic_island/shaders/nightmode.frag\"; "
                        "  [ -f \"$SHADER\" ] && hyprctl keyword decoration:screen_shader \"$SHADER\" >/dev/null 2>&1; "
                        "fi &");
        } else {
            std::system("pkill -x hyprsunset >/dev/null 2>&1; "
                        "pkill -x wlsunset >/dev/null 2>&1; "
                        "pkill -x gammastep >/dev/null 2>&1; "
                        "hyprctl keyword decoration:screen_shader \"\" >/dev/null 2>&1 &");
        }
        return true;
    }
    if (action == "qa_shot") {
        std::system("grim -g \"$(slurp)\" ~/Pictures/Screenshots/screenshot_$(date +%Y-%m-%d_%H-%M-%S).png &");
        return true;
    }
    if (action == "qa_power") {
        std::system("if command -v dynamic-island-powermenu >/dev/null 2>&1; then dynamic-island-powermenu & "
                    "elif [ -x \"$HOME/.local/bin/dynamic-island-powermenu\" ]; then \"$HOME/.local/bin/dynamic-island-powermenu\" & "
                    "elif [ -x \"$HOME/Projects/dynamic_island/scripts/power_menu.sh\" ]; then \"$HOME/Projects/dynamic_island/scripts/power_menu.sh\" & "
                    "elif command -v wlogout >/dev/null 2>&1; then wlogout -b 2 & "
                    "elif command -v rofi >/dev/null 2>&1; then "
                    "  CHOICE=$(echo -e \"󰐥  Shutdown\\n󰜉  Restart\\n󰌾  Lock Screen\\n󰤄  Suspend\\n󰗽  Log Out\" | rofi -dmenu -i -p \"Power Menu\"); "
                    "  case \"$CHOICE\" in *\"Shutdown\"*) systemctl poweroff;; *\"Restart\"*) systemctl reboot;; *\"Lock\"*) hyprlock &;; *\"Suspend\"*) systemctl suspend;; *\"Log Out\"*) hyprctl dispatch exit;; esac; "
                    "else systemctl poweroff; fi &");
        return true;
    }
    return false;
}

} // namespace di
