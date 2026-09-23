#include "quickaction_module.hpp"
#include <cstdlib>
#include <cmath>
#include <vector>

namespace di {

QuickActionModule::QuickActionModule() {}

void QuickActionModule::update() {}

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
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Header
    pango_layout_set_text(layout, "Quick Controls", -1);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // Row of 6 buttons: WiFi, BT, Mute, Night, Shot, Power
    struct ActionBtn {
        std::string icon;
        std::string label;
        std::string action;
    };

    std::vector<ActionBtn> buttons = {
        {"󰖩", "WiFi", "qa_wifi"},
        {"󰂯", "BT", "qa_bt"},
        {"󰕾", "Mute", "qa_mute"},
        {"󰃞", "Night", "qa_night"},
        {"󰹑", "Shot", "qa_shot"},
        {"󰐥", "Power", "qa_power"}
    };

    int btn_w = 44;
    int btn_h = 36;
    int btn_y = 30;
    int total_btns_w = buttons.size() * btn_w + (buttons.size() - 1) * 8;
    int start_x = (w - total_btns_w) / 2;

    for (size_t i = 0; i < buttons.size(); ++i) {
        int bx = start_x + i * (btn_w + 8);

        cairo_new_sub_path(cr);
        double r = 8.0;
        cairo_arc(cr, bx + r, btn_y + r, r, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, bx + btn_w - r, btn_y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, bx + btn_w - r, btn_y + btn_h - r, r, 0, M_PI / 2);
        cairo_arc(cr, bx + r, btn_y + btn_h - r, r, M_PI / 2, M_PI);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.9);
        cairo_fill(cr);

        // Icon
        pango_layout_set_text(layout, buttons[i].icon.c_str(), -1);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 4);
        pango_cairo_show_layout(cr, layout);

        // Label
        PangoFontDescription* small_desc = pango_font_description_copy(font_desc);
        pango_font_description_set_size(small_desc, 8 * PANGO_SCALE);
        pango_layout_set_font_description(layout, small_desc);
        pango_layout_set_text(layout, buttons[i].label.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.8);
        cairo_move_to(cr, bx + (btn_w - tw) / 2, btn_y + 20);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(small_desc);
        pango_layout_set_font_description(layout, font_desc);

        hitboxes.push_back({bx, btn_y, btn_w, btn_h, buttons[i].action, ""});
    }

    g_object_unref(layout);
}

bool QuickActionModule::handle_click(const std::string& action, const std::string& param) {
    if (action == "qa_wifi") {
        std::system("nmcli radio wifi | grep -q enabled && nmcli radio wifi off || nmcli radio wifi on &");
        return true;
    }
    if (action == "qa_bt") {
        std::system("bluetoothctl show | grep -q 'Powered: yes' && bluetoothctl power off || bluetoothctl power on &");
        return true;
    }
    if (action == "qa_mute") {
        std::system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle &");
        return true;
    }
    if (action == "qa_night") {
        std::system("pkill hyprsunset || hyprsunset --temperature 4500 &");
        return true;
    }
    if (action == "qa_shot") {
        std::system("grim -g \"$(slurp)\" ~/Pictures/Screenshots/screenshot_$(date +%Y-%m-%d_%H-%M-%S).png &");
        return true;
    }
    if (action == "qa_power") {
        std::system("which wlogout >/dev/null && wlogout & || hyprlock &");
        return true;
    }
    return false;
}

} // namespace di
