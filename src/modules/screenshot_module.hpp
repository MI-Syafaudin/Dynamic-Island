#pragma once

#include "module_base.hpp"

namespace di {

class ScreenshotModule : public ModuleBase {
public:
    ScreenshotModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;

    void take_screenshot(bool area_select);
    void set_captured(const std::string& path);
    const std::string& get_last_path() const { return m_last_path; }

private:
    std::string m_last_path;
};

} // namespace di
