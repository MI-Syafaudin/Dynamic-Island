#pragma once

#include "module_base.hpp"
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>

namespace di {

class MediaModule : public ModuleBase {
public:
    MediaModule();
    ~MediaModule() override;

    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    bool handle_click(const std::string& action, const std::string& param) override;

    bool is_playing() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_status == "Playing";
    }
    bool has_media() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_title.empty();
    }
    std::string get_status() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_status;
    }
    std::string get_title() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_title;
    }
    std::string get_artist() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_artist;
    }

    void play_pause();
    void next_track();
    void prev_track();

    bool needs_marquee_animation() const { return m_needs_marquee; }
    void on_expanded();

private:
    mutable std::mutex m_mutex;
    std::string m_status = "Stopped";
    std::string m_title;
    std::string m_artist;
    std::string m_player_name;

    std::string m_last_title;
    std::string m_last_artist;
    std::chrono::steady_clock::time_point m_anim_start_time;
    bool m_needs_marquee = false;

    // Worker thread for background playerctl queries
    std::thread m_worker_thread;
    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    std::atomic<bool> m_worker_running{false};
    std::atomic<bool> m_request_query{false};

    void worker_loop();
    void query_mpris();
    void trigger_async_query();
    void draw_marquee_text(
        cairo_t* cr,
        PangoFontDescription* font_desc,
        const std::string& text,
        int area_x,
        int area_w,
        int y,
        const ColorRGBA& color,
        bool is_bold,
        bool& out_overflow
    );
};

} // namespace di
