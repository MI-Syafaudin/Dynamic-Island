#include "quickaction_module.hpp"
#include "../hyprland_ipc.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <string_view>
#include <cctype>
#include <initializer_list>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <cstring>

namespace di {

namespace {

bool is_proc_running(std::initializer_list<const char*> names) {
    DIR* dir = opendir("/proc");
    if (!dir) return false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

        char comm_path[64];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
        FILE* fp = fopen(comm_path, "r");
        if (fp) {
            char comm[64];
            if (fgets(comm, sizeof(comm), fp)) {
                size_t len = strlen(comm);
                while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r')) {
                    comm[--len] = '\0';
                }
                for (const char* name : names) {
                    if (strcmp(comm, name) == 0) {
                        fclose(fp);
                        closedir(dir);
                        return true;
                    }
                }
            }
            fclose(fp);
        }
    }
    closedir(dir);
    return false;
}

std::string to_lower_copy(std::string_view sv) {
    std::string res;
    res.reserve(sv.size());
    for (char c : sv) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}

bool contains_any(std::string_view haystack, std::initializer_list<std::string_view> needles) {
    for (auto needle : needles) {
        if (haystack.find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// Truncates string to max_chars (codepoints) safely without splitting UTF-8 sequences
std::string truncate_utf8(std::string_view str, size_t max_chars) {
    size_t count = 0;
    size_t byte_idx = 0;
    while (byte_idx < str.size() && count < max_chars) {
        unsigned char c = static_cast<unsigned char>(str[byte_idx]);
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        if (byte_idx + len > str.size()) break;
        byte_idx += len;
        count++;
    }
    std::string res(str.substr(0, byte_idx));
    while (!res.empty() && (res.back() == ' ' || res.back() == '\t')) {
        res.pop_back();
    }
    return res;
}

std::string map_bt_device_icon(const std::string& icon_field, const std::string& name_field) {
    std::string icon_l = to_lower_copy(icon_field);
    std::string name_l = to_lower_copy(name_field);

    // 1. Headphone / Earbuds / TWS
    if (icon_l.find("audio-headphones") != std::string::npos ||
        contains_any(name_l, {"bud", "airpod", "headphone", "wh-", "wf-", "tws"})) {
        return "󰋋";
    }

    // 2. Headset
    if (icon_l.find("audio-headset") != std::string::npos ||
        contains_any(name_l, {"headset", "handsfree"})) {
        return "󰋎";
    }

    // 3. Speaker / Audio Card
    if (contains_any(icon_l, {"audio-card", "audio-speakers", "multimedia-player"}) ||
        contains_any(name_l, {"speaker", "soundbar", "jbl", "bose", "sony"})) {
        return "󰓆";
    }

    // 4. Mouse
    if (icon_l.find("input-mouse") != std::string::npos ||
        contains_any(name_l, {"mouse", "mx master", "logitech g"})) {
        return "󰍽";
    }

    // 5. Keyboard
    if (icon_l.find("input-keyboard") != std::string::npos ||
        contains_any(name_l, {"keyboard", "keychron", "k380"})) {
        return "󰌒";
    }

    // 6. Gamepad / Controller
    if (icon_l.find("input-gaming") != std::string::npos ||
        contains_any(name_l, {"controller", "gamepad", "dualshock", "dualsense", "xbox", "8bitdo"})) {
        return "󰉩";
    }

    // 7. Smartphone / Phone
    if ((icon_l.find("phone") != std::string::npos && icon_l.find("audio") == std::string::npos) ||
        (contains_any(name_l, {"phone", "iphone", "galaxy", "redmi", "poco", "pixel"}) && name_l.find("headphone") == std::string::npos)) {
        return "󰄜";
    }

    // 8. Komputer / Laptop
    if (icon_l.find("computer") != std::string::npos || icon_l.find("laptop") != std::string::npos ||
        contains_any(name_l, {"computer", "laptop"})) {
        return "󰌢";
    }

    // 9. Perangkat Lain (Default Connected)
    return "󰂰";
}

} // anonymous namespace

QuickActionModule::QuickActionModule(const Config* config) : m_config(config) {
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

    std::string bt_icon_val = "󰂲";
    std::string bt_label_val = "Bluetooth";

    if (!bt_on) {
        std::lock_guard<std::mutex> lock(m_bt_info_mutex);
        m_bt_icon = bt_icon_val;
        m_bt_label = bt_label_val;
    } else {
        bt_icon_val = "󰂯";
        bt_label_val = "Bluetooth";

        bool connected = false;
        std::string found_icon;
        std::string found_name;

        auto parse_info_stream = [&](FILE* fp) {
            char buf[512];
            bool dev_connected = false;
            std::string dev_icon;
            std::string dev_name;
            std::string dev_alias;

            auto extract_val = [](const std::string& line, const std::string& key) -> std::string {
                size_t pos = line.find(key);
                if (pos == std::string::npos) return "";
                size_t start = pos + key.length();
                while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                    start++;
                }
                std::string val = line.substr(start);
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ' || val.back() == '\t')) {
                    val.pop_back();
                }
                return val;
            };

            while (fgets(buf, sizeof(buf), fp)) {
                std::string line = buf;
                if (line.rfind("Device ", 0) == 0 && (!dev_name.empty() || !dev_alias.empty())) {
                    if (dev_connected) {
                        connected = true;
                        found_icon = dev_icon;
                        found_name = !dev_name.empty() ? dev_name : dev_alias;
                        return;
                    }
                    dev_connected = false;
                    dev_icon.clear();
                    dev_name.clear();
                    dev_alias.clear();
                }

                if (line.find("Connected: yes") != std::string::npos) {
                    dev_connected = true;
                }
                if (line.find("Icon:") != std::string::npos) {
                    std::string v = extract_val(line, "Icon:");
                    if (!v.empty()) dev_icon = v;
                }
                if (line.find("Name:") != std::string::npos) {
                    std::string v = extract_val(line, "Name:");
                    if (!v.empty()) dev_name = v;
                }
                if (line.find("Alias:") != std::string::npos) {
                    std::string v = extract_val(line, "Alias:");
                    if (!v.empty()) dev_alias = v;
                }
            }

            if (dev_connected) {
                connected = true;
                found_icon = dev_icon;
                found_name = !dev_name.empty() ? dev_name : dev_alias;
            }
        };

        // 1. Query device info via bluetoothctl info
        FILE* fp_info = popen("bluetoothctl info 2>/dev/null", "r");
        if (fp_info) {
            parse_info_stream(fp_info);
            pclose(fp_info);
        }

        // 2. Fallback: on Linux BlueZ batch mode, if bluetoothctl info requires device address, query actively connected devices
        if (!connected) {
            FILE* fp_conn = popen("for dev in $(bluetoothctl devices Connected 2>/dev/null | cut -d' ' -f2); do bluetoothctl info \"$dev\" 2>/dev/null; done", "r");
            if (fp_conn) {
                parse_info_stream(fp_conn);
                pclose(fp_conn);
            }
        }

        if (connected) {
            bt_icon_val = map_bt_device_icon(found_icon, found_name);
            if (!found_name.empty()) {
                std::string truncated = truncate_utf8(found_name, 12);
                if (!truncated.empty()) {
                    bt_label_val = truncated;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_bt_info_mutex);
            m_bt_icon = bt_icon_val;
            m_bt_label = bt_label_val;
        }
    }

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

    // 4. Dark mode state: Check gsettings color-scheme first (direct standard)
    bool dark_on = false;
    FILE* fp_dark = popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (fp_dark) {
        char dbuf[128];
        std::string dout;
        while (fgets(dbuf, sizeof(dbuf), fp_dark)) dout += dbuf;
        pclose(fp_dark);
        if (dout.find("prefer-dark") != std::string::npos) {
            dark_on = true;
        } else if (dout.find("prefer-light") != std::string::npos) {
            dark_on = false;
        } else {
            // Fallback if "default" or unset: check gtk-theme
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
    }
    m_dark_active.store(dark_on);

    // 5. Night light state: check Hyprland screen_shader via direct Unix socket IPC (0 subprocesses!)
    bool eye_active = false;
    std::string sout = HyprlandIpc::request("j/getoption decoration:screen_shader");
    size_t str_pos = sout.find("\"str\":");
    if (str_pos != std::string::npos) {
        size_t q1 = sout.find('"', str_pos + 6);
        size_t q2 = (q1 != std::string::npos) ? sout.find('"', q1 + 1) : std::string::npos;
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
            std::string val = sout.substr(q1 + 1, q2 - q1 - 1);
            if (!val.empty() && val != "[[EMPTY]]" && val != "none") {
                eye_active = true;
            }
        }
    }

    if (!eye_active) {
        // Check if hyprsunset, wlsunset, or gammastep process is running via lightweight /proc scan (0 forks!)
        eye_active = is_proc_running({"hyprsunset", "wlsunset", "gammastep"});
    }
    m_night_active.store(eye_active);
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

    std::string bt_icon;
    std::string bt_label;
    {
        std::lock_guard<std::mutex> lock(m_bt_info_mutex);
        bt_icon = m_bt_icon;
        bt_label = m_bt_label;
    }

    std::vector<ActionBtn> buttons = {
        {wifi ? "󰖩" : "󰖪", "WiFi", "qa_wifi", wifi, config.colors.accent_blue},
        {bt ? bt_icon : "󰂲", bt ? bt_label : "Bluetooth", "qa_bt", bt, config.colors.accent_blue},
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

std::string QuickActionModule::get_shader_path() const {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";
    std::vector<std::string> candidates = {
        home_str + "/.config/dynamic-island/shaders/nightmode.frag",
        home_str + "/Projects/dynamic_island/shaders/nightmode.frag",
        "/usr/local/share/dynamic-island/shaders/nightmode.frag",
        "/etc/xdg/dynamic-island/shaders/nightmode.frag"
    };

    for (const auto& path : candidates) {
        if (!path.empty() && access(path.c_str(), R_OK) == 0) {
            return path;
        }
    }
    return "";
}

void QuickActionModule::set_eye_comfort(bool enable) {
    m_night_active.store(enable);

    if (enable) {
        std::string shader = get_shader_path();
        bool applied = false;

        if (!shader.empty()) {
            // 1. Try Hyprland 0.56+ Lua eval over native Unix socket (0 forks, sub-millisecond)
            // Note: damage_tracking is set to 0 while shader is active to completely prevent
            // buffer-swap flickering, color blinking, and stuttering across KMS scanout planes
            std::string lua_cmd = "eval hl.config({ decoration = { screen_shader = \"" + shader + "\" }, debug = { damage_tracking = 0 } })";
            std::string resp = HyprlandIpc::request(lua_cmd);
            if (resp == "ok") {
                applied = true;
            } else {
                // 2. Try legacy Hyprland keyword
                std::string kw_cmd = "keyword decoration:screen_shader " + shader + "; keyword debug:damage_tracking 0";
                std::string kw_resp = HyprlandIpc::request(kw_cmd);
                if (kw_resp.find("ok") != std::string::npos || (kw_resp.empty() && access(shader.c_str(), R_OK) == 0)) {
                    applied = true;
                }
            }
        }

        // 3. Fallback: if Hyprland shader wasn't applied, check for standalone sunset utilities
        if (!applied) {
            if (m_config && !m_config->nightlight_binary.empty()) {
                std::string cmd = m_config->nightlight_binary + " --temperature 4500 &";
                if (m_config->nightlight_binary.find("wlsunset") != std::string::npos) {
                    cmd = m_config->nightlight_binary + " -t 4500 &";
                } else if (m_config->nightlight_binary.find("gammastep") != std::string::npos) {
                    cmd = m_config->nightlight_binary + " -O 4500 &";
                }
                std::system(cmd.c_str());
            } else {
                std::system("if command -v hyprsunset >/dev/null 2>&1; then hyprsunset --temperature 4500 & "
                            "elif command -v wlsunset >/dev/null 2>&1; then wlsunset -t 4500 & "
                            "elif command -v gammastep >/dev/null 2>&1; then gammastep -O 4500 & "
                            "fi &");
            }
        }
    } else {
        // Disabling Eye Comfort:
        // 1. Reset Hyprland screen_shader and restore default damage_tracking = 2 via Lua eval & keyword
        HyprlandIpc::request("eval hl.config({ decoration = { screen_shader = \"\" }, debug = { damage_tracking = 2 } })");
        HyprlandIpc::request("keyword decoration:screen_shader \"\"; keyword debug:damage_tracking 2");

        // 2. Kill standalone daemons if running
        std::system("pkill -x hyprsunset >/dev/null 2>&1; "
                    "pkill -x wlsunset >/dev/null 2>&1; "
                    "pkill -x gammastep >/dev/null 2>&1 &");
    }
}

void QuickActionModule::set_dark_mode(bool enable) {
    m_dark_active.store(enable);

    // Call sync_dark_mode.lua helper script or fallback commands
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";
    std::string script1 = home_str + "/.local/bin/dynamic-island-sync-dark";
    std::string script2 = home_str + "/.config/dynamic-island/scripts/sync_dark_mode.lua";
    std::string script3 = home_str + "/Projects/dynamic_island/scripts/sync_dark_mode.lua";
    std::string script4 = "/usr/local/bin/dynamic-island-sync-dark";

    std::string arg = enable ? "on" : "off";

    std::string found_script = "";
    if (access(script1.c_str(), X_OK) == 0) found_script = script1;
    else if (access(script2.c_str(), R_OK) == 0) found_script = script2;
    else if (access(script3.c_str(), R_OK) == 0) found_script = script3;
    else if (access(script4.c_str(), X_OK) == 0) found_script = script4;

    if (!found_script.empty()) {
        if (access(found_script.c_str(), X_OK) == 0) {
            std::system(("\"" + found_script + "\" " + arg + " >/dev/null 2>&1 &").c_str());
        } else {
            std::system(("luajit \"" + found_script + "\" " + arg + " >/dev/null 2>&1 &").c_str());
        }
    } else {
        std::string scheme = enable ? "prefer-dark" : "prefer-light";
        std::string gtk_theme = enable ? "Adwaita-dark" : "Adwaita";
        int dark_num = enable ? 1 : 0;

        std::string cmd = 
            "gsettings set org.gnome.desktop.interface color-scheme '" + scheme + "' & "
            "gsettings set org.gnome.desktop.interface gtk-theme '" + gtk_theme + "' & "
            "which dconf >/dev/null 2>&1 && dconf write /org/gnome/desktop/interface/color-scheme \"'" + scheme + "'\" & "
            "mkdir -p ~/.config/gtk-3.0 ~/.config/gtk-4.0 && "
            "for sfile in ~/.config/gtk-3.0/settings.ini ~/.config/gtk-4.0/settings.ini; do "
            "  echo -e \"[Settings]\\ngtk-application-prefer-dark-theme=" + std::to_string(dark_num) + "\\ngtk-theme-name=" + gtk_theme + "\" > \"$sfile\"; "
            "done &";
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
        {
            std::lock_guard<std::mutex> lock(m_bt_info_mutex);
            m_bt_icon = new_val ? "󰂯" : "󰂲";
            m_bt_label = "Bluetooth";
        }
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
        set_eye_comfort(!m_night_active.load());
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
