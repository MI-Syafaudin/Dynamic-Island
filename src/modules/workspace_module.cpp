#include "workspace_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <cmath>

namespace di {

WorkspaceModule::WorkspaceModule() {
    m_visible_workspaces = {1, 2, 3, 4, 5};
    refresh_active_workspace();
}

void WorkspaceModule::refresh_active_workspace() {
    FILE* fp = popen("hyprctl activeworkspace -j 2>/dev/null", "r");
    if (!fp) return;

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        result += buffer;
    }
    pclose(fp);

    size_t id_pos = result.find("\"id\":");
    if (id_pos != std::string::npos) {
        size_t start = result.find_first_of("0123456789", id_pos);
        if (start != std::string::npos) {
            int ws = std::atoi(&result[start]);
            if (ws > 0) m_active_workspace = ws;
        }
    }
}

void WorkspaceModule::set_active_workspace(int ws) {
    if (ws > 0) {
        m_active_workspace = ws;
    }
}

void WorkspaceModule::update() {
    // Typically driven by Hyprland IPC socket2 events
}

void WorkspaceModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    for (int ws : m_visible_workspaces) {
        bool is_active = (ws == m_active_workspace);
        std::string label = std::to_string(ws);
        pango_layout_set_text(layout, label.c_str(), -1);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);

        int item_w = std::max(tw + 10, 20);
        int item_h = 20;
        int item_y = y + (h - item_h) / 2;

        if (is_active) {
            // Draw active pill badge
            cairo_new_sub_path(cr);
            double r = item_h / 2.0;
            cairo_arc(cr, current_x + r, item_y + r, r, M_PI / 2, 3 * M_PI / 2);
            cairo_arc(cr, current_x + item_w - r, item_y + r, r, -M_PI / 2, M_PI / 2);
            cairo_close_path(cr);

            cairo_set_source_rgba(cr, config.colors.accent.r, config.colors.accent.g, config.colors.accent.b, 0.95);
            cairo_fill(cr);

            // Active text color (dark on bright accent)
            cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, 1.0);
        } else {
            // Inactive text color
            cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.7);
        }

        cairo_move_to(cr, current_x + (item_w - tw) / 2, item_y + (item_h - th) / 2);
        pango_cairo_show_layout(cr, layout);

        hitboxes.push_back({current_x, item_y, item_w, item_h, "workspace", label});
        current_x += item_w + 4;
    }

    g_object_unref(layout);
    current_x += 4;
}

bool WorkspaceModule::handle_click(const std::string& action, const std::string& param) {
    if (action == "workspace") {
        std::string cmd = "hyprctl dispatch workspace " + param + " >/dev/null 2>&1 &";
        std::system(cmd.c_str());
        return true;
    }
    return false;
}

} // namespace di
