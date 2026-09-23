#pragma once

#include "module_base.hpp"

namespace di {

class NotificationModule : public ModuleBase {
public:
    NotificationModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;

    void post_notification(const std::string& app_name, const std::string& message);
    bool has_notification() const { return !m_message.empty(); }

private:
    std::string m_app_name;
    std::string m_message;
};

} // namespace di
