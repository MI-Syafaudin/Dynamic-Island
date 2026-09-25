#pragma once

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <vector>
#include <memory>
#include "config.hpp"
#include "modules/module_base.hpp"

namespace di {

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const Config& config);
    void update_font(const Config& config);

    int calculate_compact_width(
        const Config& config,
        const std::vector<std::shared_ptr<ModuleBase>>& modules
    );

    void render_island(
        cairo_t* cr,
        int width,
        int height,
        double corner_radius,
        IslandMode mode,
        const Config& config,
        const std::vector<std::shared_ptr<ModuleBase>>& modules,
        std::vector<HitBox>& hitboxes,
        int brightness_val = 65
    );

private:
    PangoFontDescription* m_font_desc = nullptr;

    void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);
    void draw_brightness_expanded(cairo_t* cr, const Config& config, int w, int h, int brightness_val, std::vector<HitBox>& hitboxes);
};

} // namespace di
