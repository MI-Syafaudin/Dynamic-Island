#pragma once

#include "module_base.hpp"

namespace di {

class NetworkModule : public ModuleBase {
public:
    NetworkModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;

    bool is_connected() const { return m_connected; }
    const std::string& get_ssid() const { return m_ssid; }
    const std::string& get_ip() const { return m_ip; }

private:
    bool m_connected = false;
    bool m_is_wifi = false;
    std::string m_ssid;
    std::string m_interface;
    std::string m_ip;

    void query_network();
    void query_local_ip();
};

} // namespace di
