#pragma once

#include "module_base.hpp"
#include <vector>
#include <set>

namespace di {

class WorkspaceModule : public ModuleBase {
public:
    WorkspaceModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    void set_active_workspace(int ws);
    int get_active_workspace() const { return m_active_workspace; }

private:
    int m_active_workspace = 1;
    std::vector<int> m_visible_workspaces;
    void refresh_active_workspace();
    void update_visible_workspaces();
};

} // namespace di
