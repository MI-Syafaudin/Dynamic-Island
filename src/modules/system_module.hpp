#pragma once

#include "module_base.hpp"

namespace di {

class SystemModule : public ModuleBase {
public:
    SystemModule();
    ~SystemModule() override;
    void update() override { update(false); }
    void update(bool full);
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;

    int get_cpu_percent() const { return m_cpu_percent; }
    int get_ram_percent() const { return m_ram_percent; }
    int get_swap_percent() const { return m_swap_percent; }
    int get_gpu_percent() const { return m_gpu_percent; }
    int get_cpu_temp() const { return m_cpu_temp; }
    int get_disk_percent() const { return m_disk_percent; }
    double get_disk_used_gb() const { return m_disk_used_gb; }
    double get_disk_total_gb() const { return m_disk_total_gb; }

    void configure(double cpu_alpha, double gpu_alpha, const std::string& gpu_name_override, const std::string& temp_path_override, const std::string& disk_path_override = "/");
    void get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr = nullptr);

private:
    int m_cpu_percent = 0;
    int m_ram_percent = 0;
    int m_swap_percent = 0;
    int m_gpu_percent = 0;
    int m_cpu_temp = 0;
    int m_disk_percent = 0;

    double m_ram_used_gb = 0.0;
    double m_ram_total_gb = 0.0;
    double m_swap_used_gb = 0.0;
    double m_swap_total_gb = 0.0;
    double m_disk_used_gb = 0.0;
    double m_disk_total_gb = 0.0;
    bool m_is_zram = false;

    double m_cpu_smoothing_alpha = 0.80;
    double m_gpu_smoothing_alpha = 0.75;

    unsigned long long m_prev_total_jiffies = 0;
    unsigned long long m_prev_work_jiffies = 0;

    std::string m_gpu_busy_path;
    std::string m_intel_act_freq_path;
    std::string m_intel_max_freq_path;
    std::string m_gpu_name = "(Radeon Vega 3)";
    std::string m_temp_path = "/sys/class/thermal/thermal_zone0/temp";
    std::string m_disk_path = "/";

    // Persistent file descriptors for zero-allocation pread()
    int m_fd_stat = -1;
    int m_fd_meminfo = -1;
    int m_fd_temp = -1;
    int m_fd_gpu = -1;
    int m_fd_intel_act = -1;
    int m_fd_intel_max = -1;

    void detect_gpu_sysfs();
    void update_cpu();
    void update_ram();
    void update_gpu();
    void update_temp();
    void update_disk();
};

} // namespace di
