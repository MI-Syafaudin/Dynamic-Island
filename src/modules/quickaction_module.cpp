#include "quickaction_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

namespace di {

QuickActionModule::QuickActionModule() {
    query_states();
}

void QuickActionModule::query_states() {
    // 1. WiFi state: nmcli radio wifi
    FILE* fp = popen("nmcli radio wifi 2>/dev/null", "r");
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            std::string out = buf;
            m_wifi_active = (out.find("enabled") != std::string::npos);
        }
        pclose(fp);
    }

    // 2. Bluetooth state: bluetoothctl show
    fp = popen("bluetoothctl show 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            std::string line = buf;
            if (line.find("Powered: yes") != std::string::npos) {
                m_bt_active = true;
                break;
            } else if (line.find("Powered: no") != std::string::npos) {
                m_bt_active = false;
                break;
            }
        }
        pclose(fp);
    }

    // 3. Audio Mute state: wpctl get-volume @DEFAULT_AUDIO_SINK@
    fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (fp) {
        char buf[128];
        std::string out;
        while (fgets(buf, sizeof(buf), fp)) out += buf;
        pclose(fp);
        m_mute_active = (out.find("[MUTED]") != std::string::npos);
    }

    // 4. Night light state: check processes (hyprsunset, wlsunset, gammastep) or Hyprland screen_shader
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
                    shader_active = true;
                }
            }
        }
    }
    m_night_active = proc_running || shader_active;
}

void QuickActionModule::update() {
    query_states();
}

void QuickActionModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    // Optionally a subtle dots / menu icon at the right edge
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
    (void)h;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Header centered
    pango_layout_set_text(layout, "Quick Controls", -1);
    int htw, hth;
    pango_layout_get_pixel_size(layout, &htw, &hth);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, (w - htw) / 2, 10);
    pango_cairo_show_layout(cr, layout);

    // Row of 6 buttons: WiFi, BT, Mute, Night, Shot, Power
    struct ActionBtn {
        std::string icon;
        std::string label;
        std::string action;
        bool is_active;
        ColorRGBA active_color;
    };

    std::vector<ActionBtn> buttons = {
        {m_wifi_active ? "󰖩" : "󰖪", "WiFi", "qa_wifi", m_wifi_active, config.colors.accent_blue},
        {m_bt_active ? "󰂯" : "󰂲", "BT", "qa_bt", m_bt_active, config.colors.accent_blue},
        {m_mute_active ? "󰖁" : "󰕾", m_mute_active ? "Muted" : "Mute", "qa_mute", m_mute_active, config.colors.danger},
        {"󰃞", "Night", "qa_night", m_night_active, config.colors.warning},
        {"󰹑", "Shot", "qa_shot", false, config.colors.accent_blue},
        {"󰐥", "Power", "qa_power", false, config.colors.danger}
    };

    int btn_w = 44;
    int btn_h = 36;
    int btn_y = 30;
    int total_btns_w = buttons.size() * btn_w + (buttons.size() - 1) * 8;
    int start_x = (w - total_btns_w) / 2;

    PangoFontDescription* small_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(small_desc, 8 * PANGO_SCALE);

    for (size_t i = 0; i < buttons.size(); ++i) {
        int bx = start_x + i * (btn_w + 8);

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
            if (buttons[i].action == "qa_night") {
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
            if (buttons[i].action == "qa_night") {
                cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 1.0);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            }
        } else {
            cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 0.75);
        }
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 4);
        pango_cairo_show_layout(cr, layout);

        // Label
        pango_layout_set_font_description(layout, small_desc);
        pango_layout_set_text(layout, buttons[i].label.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (buttons[i].is_active) {
            if (buttons[i].action == "qa_night") {
                cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.9);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
            }
        } else {
            cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.7);
        }
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 20);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({bx, btn_y, btn_w, btn_h, buttons[i].action, ""});
    }

    pango_font_description_free(small_desc);
    g_object_unref(layout);
}

bool QuickActionModule::handle_click(const std::string& action, const std::string& param) {
    (void)param;
    if (action == "qa_wifi") {
        m_wifi_active = !m_wifi_active;
        if (m_wifi_active) {
            std::system("nmcli radio wifi on &");
        } else {
            std::system("nmcli radio wifi off &");
        }
        return true;
    }
    if (action == "qa_bt") {
        m_bt_active = !m_bt_active;
        if (m_bt_active) {
            std::system("bluetoothctl power on &");
        } else {
            std::system("bluetoothctl power off &");
        }
        return true;
    }
    if (action == "qa_mute") {
        m_mute_active = !m_mute_active;
        std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle &");
        return true;
    }
    if (action == "qa_night") {
        m_night_active = !m_night_active;
        if (m_night_active) {
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
