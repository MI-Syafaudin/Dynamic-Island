#pragma once

#include "module_base.hpp"

namespace di {

class WindowModule : public ModuleBase {
public:
    WindowModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;

    void set_active_window(const std::string& app_class, const std::string& app_title);
    const std::string& get_window_title() const { return m_title; }
    const std::string& get_window_class() const { return m_class; }
    const std::string& get_cached_icon() const { return m_cached_icon; }
    static std::string get_app_icon(const std::string& app_class, const std::string& app_title);

private:
    std::string m_class;
    std::string m_title;
    std::string m_cached_icon;
    void refresh_active_window();
};

} // namespace di
