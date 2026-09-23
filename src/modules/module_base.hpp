#pragma once

#include <string>
#include <vector>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include "../config.hpp"

namespace di {

struct HitBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string action;
    std::string param;
};

enum class IslandMode {
    Idle,
    Expanded_Audio,
    Expanded_Brightness,
    Expanded_Media,
    Expanded_System,
    Expanded_Clock,
    Expanded_Network,
    Expanded_Notification,
    Expanded_Screenshot,
    Expanded_QuickActions
};

class ModuleBase {
public:
    virtual ~ModuleBase() = default;
    virtual void update() = 0;
    virtual void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) = 0;
    virtual void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {}
    virtual bool handle_click(const std::string& action, const std::string& param) { return false; }
};

} // namespace di
