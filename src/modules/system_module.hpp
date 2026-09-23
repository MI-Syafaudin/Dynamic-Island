#pragma once

#include "module_base.hpp"

namespace di {

class SystemModule : public ModuleBase {
public:
    SystemModule();
    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;

    int get_cpu_percent() const { return m_cpu_percent; }
    int get_ram_percent() const { return m_ram_percent; }
    int get_gpu_percent() const { return m_gpu_percent; }
    int get_cpu_temp() const { return m_cpu_temp; }

private:
    int m_cpu_percent = 0;
    int m_ram_percent = 0;
    int m_gpu_percent = 0;
    int m_cpu_temp = 0;

    double m_ram_used_gb = 0.0;
    double m_ram_total_gb = 0.0;

    unsigned long long m_prev_total_jiffies = 0;
    unsigned long long m_prev_work_jiffies = 0;

    std::string m_gpu_busy_path;

    void update_cpu();
    void update_ram();
    void update_gpu();
    void update_temp();
};

} // namespace di
