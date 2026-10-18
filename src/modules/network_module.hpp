#pragma once

#include "module_base.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace di {

struct PingSample {
    bool success = false;
    double rtt = 0.0;
};

class NetworkModule : public ModuleBase {
public:
    NetworkModule();
    ~NetworkModule() override;

    void update() override;
    void draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) override;
    void draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) override;
    void get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr = nullptr) override;

    void on_expanded();
    void on_collapsed();
    void set_ping_target(const std::string& target) {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_ping_target = target;
    }
    void set_ping_label(const std::string& label) {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_ping_label = label;
    }
    void set_expanded_interval(int ms) {
        m_expanded_interval_ms = (ms > 50) ? ms : 400;
    }

    bool is_connected() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_connected;
    }
    bool is_wifi() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_is_wifi;
    }
    std::string get_ssid() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_ssid;
    }
    std::string get_ip() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_ip;
    }
    std::string get_interface() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_interface;
    }
    std::string get_gateway_ip() const {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        return m_gateway_ip;
    }

    double get_ping() const;
    double get_jitter() const;
    double get_packet_loss() const;
    bool has_ping_data() const;
    double get_gateway_ping() const;
    double get_download_speed() const;
    double get_upload_speed() const;
    int get_wifi_signal() const;

    bool check_and_clear_dirty() {
        bool expected = true;
        return m_dirty.compare_exchange_strong(expected, false);
    }

    static std::string format_speed(double bytes_per_sec);

    // Native Socket ping engine: ICMP unprivileged -> TCP SYN fallback
    static uint16_t calculate_checksum(const void* b, int len);
    static bool measure_native_icmp(const std::string& ip_str, double& rtt_ms, int timeout_ms);
    static bool measure_native_tcp(const std::string& ip_str, int port, double& rtt_ms, int timeout_ms);
    static bool ping_address_with_fallback(const std::string& ip_str, const std::vector<int>& tcp_ports, double& rtt_ms, int timeout_ms = 120);

private:
    bool m_connected = false;
    bool m_is_wifi = false;
    std::string m_ssid;
    std::string m_interface;
    std::string m_ip;
    std::string m_gateway_ip;
    std::chrono::steady_clock::time_point m_last_nmcli_query;
    bool m_had_gateway = false;

    // Persistent file descriptors for zero-allocation pread()
    int m_fd_net_dev = -1;
    int m_fd_wireless = -1;

    // Real-time ping, jitter, packet loss, gateway, bandwidth, and wifi metrics
    std::string m_ping_target = "1.1.1.1";
    std::string m_ping_label = "Cloudflare";
    int m_expanded_interval_ms = 400;
    std::deque<PingSample> m_ping_history;
    mutable std::mutex m_metrics_mutex;
    double m_ping = 0.0;
    double m_jitter = 0.0;
    double m_packet_loss = 0.0;
    bool m_has_ping_data = false;
    double m_gateway_ping = 0.0;
    double m_download_speed = 0.0;
    double m_upload_speed = 0.0;
    int m_wifi_signal_pct = 0;
    int m_wifi_signal_dbm = 0;

    // Bandwidth calculation state
    uint64_t m_last_rx_bytes = 0;
    uint64_t m_last_tx_bytes = 0;
    std::chrono::steady_clock::time_point m_last_bandwidth_time;

    // Worker thread for non-blocking asynchronous measurements
    std::thread m_worker_thread;
    std::mutex m_thread_mutex;
    std::condition_variable m_thread_cv;
    std::atomic<bool> m_worker_running{false};
    std::atomic<bool> m_dirty{false};
    bool m_is_active = false;

    void query_network();
    void query_local_ip();
    void query_bandwidth();
    void query_wifi_signal();
    void worker_loop();
    void measure_ping();
    void measure_gateway_ping();
};

} // namespace di
