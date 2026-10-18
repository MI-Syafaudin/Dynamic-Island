#include "network_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>

namespace di {

uint16_t NetworkModule::calculate_checksum(const void* b, int len) {
    const uint16_t* buf = reinterpret_cast<const uint16_t*>(b);
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *reinterpret_cast<const uint8_t*>(buf);
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

bool NetworkModule::measure_native_icmp(const std::string& ip_str, double& rtt_ms, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_ICMP);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_str.c_str(), &dest.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    struct icmphdr icmp;
    std::memset(&icmp, 0, sizeof(icmp));
    icmp.type = ICMP_ECHO;
    icmp.code = 0;
    icmp.un.echo.id = htons(static_cast<uint16_t>(getpid() & 0xFFFF));
    icmp.un.echo.sequence = htons(1);
    icmp.checksum = calculate_checksum(&icmp, sizeof(icmp));

    auto t0 = std::chrono::steady_clock::now();
    ssize_t sent = sendto(sock, &icmp, sizeof(icmp), 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    if (sent < 0) {
        close(sock);
        return false;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        char recv_buf[256];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, reinterpret_cast<struct sockaddr*>(&from), &from_len);
        auto t1 = std::chrono::steady_clock::now();
        if (n >= static_cast<ssize_t>(sizeof(struct icmphdr))) {
            struct icmphdr* r_icmp = reinterpret_cast<struct icmphdr*>(recv_buf);
            if (r_icmp->type == ICMP_ECHOREPLY) {
                rtt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                close(sock);
                return true;
            }
        }
    }

    close(sock);
    return false;
}

bool NetworkModule::measure_native_tcp(const std::string& ip_str, int port, double& rtt_ms, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sock < 0) return false;

    struct sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip_str.c_str(), &dest.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();
    int rc = connect(sock, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    if (rc == 0) {
        auto t1 = std::chrono::steady_clock::now();
        rtt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        close(sock);
        return true;
    }

    if (errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP))) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
            // In TCP handshake, ECONNREFUSED still confirms remote host responded with TCP RST within 1 RTT!
            if (err == 0 || err == ECONNREFUSED) {
                auto t1 = std::chrono::steady_clock::now();
                rtt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                close(sock);
                return true;
            }
        }
    }

    close(sock);
    return false;
}

bool NetworkModule::ping_address_with_fallback(const std::string& ip_str, const std::vector<int>& tcp_ports, double& rtt_ms, int timeout_ms) {
    if (ip_str.empty()) return false;

    // 1. Linux Unprivileged ICMP Socket (Zero-privilege kernel socket)
    if (measure_native_icmp(ip_str, rtt_ms, timeout_ms)) {
        return true;
    }

    // 2. High-speed Non-blocking TCP SYN Handshake Fallback (No root / unprivileged)
    for (int port : tcp_ports) {
        if (measure_native_tcp(ip_str, port, rtt_ms, timeout_ms)) {
            return true;
        }
    }

    return false;
}

NetworkModule::NetworkModule() {
    m_fd_net_dev = open("/proc/net/dev", O_RDONLY | O_CLOEXEC);
    m_fd_wireless = open("/proc/net/wireless", O_RDONLY | O_CLOEXEC);

    query_network();
    m_worker_running = true;
    m_worker_thread = std::thread(&NetworkModule::worker_loop, this);
}

NetworkModule::~NetworkModule() {
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        m_worker_running = false;
    }
    m_thread_cv.notify_all();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    if (m_fd_net_dev >= 0) {
        close(m_fd_net_dev);
        m_fd_net_dev = -1;
    }
    if (m_fd_wireless >= 0) {
        close(m_fd_wireless);
        m_fd_wireless = -1;
    }
}

void NetworkModule::query_local_ip() {
    std::string ip = "127.0.0.1";
    std::string iface;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        char host[NI_MAXHOST];
        int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
        if (s == 0) {
            ip = host;
            iface = ifa->ifa_name;
            break;
        }
    }
    freeifaddrs(ifaddr);

    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    m_ip = ip;
    if (!iface.empty()) m_interface = iface;
}

void NetworkModule::query_network() {
    auto now = std::chrono::steady_clock::now();

    // Fast-path: Check default route in /proc/net/route without spawning any processes
    bool has_default_gateway = false;
    std::string detected_iface;
    std::string detected_gw;
    FILE* fr = fopen("/proc/net/route", "r");
    if (fr) {
        char rline[128];
        while (fgets(rline, sizeof(rline), fr)) {
            char iface[32];
            unsigned long dest = 1;
            unsigned long gw = 0;
            if (sscanf(rline, "%31s %lx %lx", iface, &dest, &gw) >= 2) {
                if (dest == 0 && strcmp(iface, "lo") != 0) {
                    has_default_gateway = true;
                    detected_iface = iface;
                    if (gw != 0) {
                        struct in_addr in;
                        in.s_addr = gw;
                        char* g_str = inet_ntoa(in);
                        if (g_str) detected_gw = g_str;
                    }
                    break;
                }
            }
        }
        fclose(fr);
    }

    if (!has_default_gateway) {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_gateway_ip = detected_gw;
        m_connected = false;
        m_is_wifi = false;
        m_ssid.clear();
        m_had_gateway = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_gateway_ip = detected_gw;
        if (!detected_iface.empty()) m_interface = detected_iface;
        // If already connected and gateway status hasn't changed, query nmcli only every 30s
        if (m_connected && m_had_gateway && !m_ssid.empty() &&
            std::chrono::duration_cast<std::chrono::seconds>(now - m_last_nmcli_query).count() < 30) {
            return;
        }
    }

    m_had_gateway = true;
    m_last_nmcli_query = now;

    FILE* fp = popen("nmcli -t -f TYPE,STATE,CONNECTION device 2>/dev/null", "r");
    if (!fp) return;

    char buffer[256];
    bool conn = false;
    bool is_wf = false;
    std::string ssid;

    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

        // format: type:state:connection
        if (line.find("wifi:connected:") == 0) {
            conn = true;
            is_wf = true;
            ssid = line.substr(15);
            break;
        } else if (line.find("ethernet:connected:") == 0) {
            conn = true;
            is_wf = false;
            ssid = "Ethernet";
            break;
        }
    }
    pclose(fp);

    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_connected = conn;
        m_is_wifi = is_wf;
        m_ssid = ssid;
    }

    if (conn) {
        query_local_ip();
        query_wifi_signal();
    }
}

void NetworkModule::update() {
    // Non-blocking from main thread: worker thread performs all network/ping queries
    std::lock_guard<std::mutex> lock(m_thread_mutex);
    m_thread_cv.notify_one();
}

void NetworkModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    bool conn = false;
    bool is_wf = false;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        conn = m_connected;
        is_wf = m_is_wifi;
    }
    std::string icon = conn ? (is_wf ? "󰖩" : "󰈀") : "󰖪";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, icon.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (conn) {
        cairo_set_source_rgba(cr, config.colors.accent_blue.r, config.colors.accent_blue.g, config.colors.accent_blue.b, 0.95);
    } else {
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.6);
    }

    cairo_move_to(cr, current_x, y + (h - th) / 2);
    pango_cairo_show_layout(cr, layout);

    hitboxes.push_back({current_x - 2, y, tw + 4, h, "toggle_expand", "network"});
    current_x += tw + 10;

    g_object_unref(layout);
}

void NetworkModule::query_bandwidth() {
    if (m_fd_net_dev < 0) {
        m_fd_net_dev = open("/proc/net/dev", O_RDONLY | O_CLOEXEC);
        if (m_fd_net_dev < 0) return;
    }

    char buf[2048];
    ssize_t n = pread(m_fd_net_dev, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    auto now = std::chrono::steady_clock::now();
    uint64_t cur_rx = 0;
    uint64_t cur_tx = 0;
    bool found = false;

    std::string iface_target;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        iface_target = m_interface;
    }

    char* line = buf;
    while (line && *line) {
        char* next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';

        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* p = line;
            while (*p == ' ') p++;

            if (iface_target.empty() || strcmp(p, iface_target.c_str()) == 0) {
                unsigned long long r_b = 0, r_p = 0, r_e = 0, r_d = 0, r_f = 0, r_fr = 0, r_c = 0, r_m = 0;
                unsigned long long t_b = 0;
                if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                           &r_b, &r_p, &r_e, &r_d, &r_f, &r_fr, &r_c, &r_m, &t_b) >= 9) {
                    cur_rx = r_b;
                    cur_tx = t_b;
                    found = true;
                    break;
                }
            }
        }

        if (next_line) line = next_line + 1;
        else break;
    }

    if (found) {
        if (m_last_bandwidth_time.time_since_epoch().count() > 0) {
            double dt = std::chrono::duration<double>(now - m_last_bandwidth_time).count();
            if (dt > 0.1) {
                double rx_diff = (cur_rx >= m_last_rx_bytes) ? static_cast<double>(cur_rx - m_last_rx_bytes) : 0.0;
                double tx_diff = (cur_tx >= m_last_tx_bytes) ? static_cast<double>(cur_tx - m_last_tx_bytes) : 0.0;

                std::lock_guard<std::mutex> lock(m_metrics_mutex);
                m_download_speed = rx_diff / dt;
                m_upload_speed = tx_diff / dt;
            }
        }
        m_last_rx_bytes = cur_rx;
        m_last_tx_bytes = cur_tx;
        m_last_bandwidth_time = now;
    }
}

void NetworkModule::query_wifi_signal() {
    bool is_wf = false;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        is_wf = m_is_wifi;
    }
    if (!is_wf) {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_wifi_signal_pct = 0;
        m_wifi_signal_dbm = 0;
        return;
    }

    if (m_fd_wireless < 0) {
        m_fd_wireless = open("/proc/net/wireless", O_RDONLY | O_CLOEXEC);
        if (m_fd_wireless < 0) return;
    }

    char buf[512];
    ssize_t n = pread(m_fd_wireless, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    int pct = 0;
    int dbm = 0;
    bool found = false;

    char* line = buf;
    while (line && *line) {
        char* next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';

        char* colon = strchr(line, ':');
        if (colon) {
            int status = 0;
            float link_qual = 0;
            float level = 0;
            if (sscanf(colon + 1, "%d %f %f", &status, &link_qual, &level) >= 3) {
                pct = std::clamp(static_cast<int>(std::round(link_qual * 100.0 / 70.0)), 0, 100);
                dbm = static_cast<int>(std::round(level));
                found = true;
                break;
            }
        }

        if (next_line) line = next_line + 1;
        else break;
    }

    if (found) {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        m_wifi_signal_pct = pct;
        m_wifi_signal_dbm = dbm;
    }
}

void NetworkModule::measure_gateway_ping() {
    std::string gw;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        gw = m_gateway_ip;
    }
    if (gw.empty()) return;

    double gw_rtt = 0.0;
    // Native unprivileged ICMP -> fallback to port 53 / 80 non-blocking TCP SYN
    bool success = ping_address_with_fallback(gw, {53, 80}, gw_rtt, 100);

    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    if (success) {
        m_gateway_ping = gw_rtt;
    } else {
        m_gateway_ping = -1.0;
    }
}

void NetworkModule::worker_loop() {
    while (m_worker_running) {
        bool active = false;
        {
            std::unique_lock<std::mutex> lock(m_thread_mutex);
            // In Idle mode: Zero polling for ping or bandwidth, wait up to 5s for network route changes or expand
            m_thread_cv.wait_for(lock, std::chrono::milliseconds(5000), [this]() {
                return !m_worker_running || m_is_active;
            });
            if (!m_worker_running) break;
            active = m_is_active;
        }

        // Query basic connection state safely in background thread
        query_network();

        if (active) {
            bool conn = false;
            {
                std::lock_guard<std::mutex> lock(m_metrics_mutex);
                conn = m_connected;
            }
            if (conn) {
                query_bandwidth();
                query_wifi_signal();
                measure_ping();
                measure_gateway_ping();
            } else {
                std::lock_guard<std::mutex> lock(m_metrics_mutex);
                m_ping_history.clear();
                m_ping = 0.0;
                m_jitter = 0.0;
                m_packet_loss = 100.0;
                m_has_ping_data = true;
                m_gateway_ping = 0.0;
                m_download_speed = 0.0;
                m_upload_speed = 0.0;
                m_wifi_signal_pct = 0;
                m_wifi_signal_dbm = 0;
            }
            m_dirty = true;

            std::unique_lock<std::mutex> lock(m_thread_mutex);
            int interval = (m_expanded_interval_ms > 50) ? m_expanded_interval_ms : 400;
            m_thread_cv.wait_for(lock, std::chrono::milliseconds(interval), [this]() {
                return !m_worker_running || !m_is_active;
            });
        }
    }
}

void NetworkModule::measure_ping() {
    std::string target;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        target = m_ping_target.empty() ? "1.1.1.1" : m_ping_target;
    }

    // Native unprivileged ICMP -> fallback to port 53 / 443 non-blocking TCP SYN
    // Perform 2 quick consecutive measurements for instantaneous RTT and jitter
    double rtt1 = 0.0;
    bool s1 = ping_address_with_fallback(target, {53, 443}, rtt1, 120);

    double rtt2 = 0.0;
    bool s2 = ping_address_with_fallback(target, {53, 443}, rtt2, 120);

    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    if (s1) m_ping_history.push_back({true, rtt1});
    else m_ping_history.push_back({false, 0.0});

    if (s2) m_ping_history.push_back({true, rtt2});
    else m_ping_history.push_back({false, 0.0});

    while (m_ping_history.size() > 10) {
        m_ping_history.pop_front();
    }

    if (s1 || s2) {
        m_ping = s2 ? rtt2 : rtt1;

        std::vector<double> succ_rtts;
        for (const auto& s : m_ping_history) {
            if (s.success) succ_rtts.push_back(s.rtt);
        }
        if (succ_rtts.size() >= 2) {
            double diff_sum = 0.0;
            for (size_t i = 1; i < succ_rtts.size(); ++i) {
                diff_sum += std::abs(succ_rtts[i] - succ_rtts[i - 1]);
            }
            m_jitter = diff_sum / (succ_rtts.size() - 1);
        } else if (s1 && s2) {
            m_jitter = std::abs(rtt2 - rtt1);
        }
    }

    int lost = 0;
    for (const auto& s : m_ping_history) {
        if (!s.success) lost++;
    }
    m_packet_loss = m_ping_history.empty() ? 0.0 : (lost * 100.0) / m_ping_history.size();

    m_has_ping_data = true;
    m_dirty = true;
}

void NetworkModule::on_expanded() {
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        m_is_active = true;
    }
    m_thread_cv.notify_all();
}

void NetworkModule::on_collapsed() {
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        m_is_active = false;
    }
    m_thread_cv.notify_all();
}

void NetworkModule::get_preferred_dimensions(PangoFontDescription* font_desc, const Config& config, double& w, double& h, cairo_t* cr) {
    (void)font_desc;
    (void)config;
    (void)cr;
    w = 380.0;
    h = 170.0;
}

std::string NetworkModule::format_speed(double bytes_per_sec) {
    if (bytes_per_sec < 1024.0) {
        return std::to_string(static_cast<int>(bytes_per_sec)) + " B/s";
    } else if (bytes_per_sec < 1024.0 * 1024.0) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(bytes_per_sec < 102400.0 ? 1 : 0) << (bytes_per_sec / 1024.0) << " KB/s";
        return ss.str();
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes_per_sec / (1024.0 * 1024.0)) << " MB/s";
        return ss.str();
    }
}

double NetworkModule::get_ping() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_ping;
}

double NetworkModule::get_jitter() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_jitter;
}

double NetworkModule::get_packet_loss() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_packet_loss;
}

bool NetworkModule::has_ping_data() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_has_ping_data;
}

double NetworkModule::get_gateway_ping() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_gateway_ping;
}

double NetworkModule::get_download_speed() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_download_speed;
}

double NetworkModule::get_upload_speed() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_upload_speed;
}

int NetworkModule::get_wifi_signal() const {
    std::lock_guard<std::mutex> lock(m_metrics_mutex);
    return m_wifi_signal_pct;
}

void NetworkModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    // Snapshot metrics and network state under lock
    double ping_val = 0.0;
    double jitter_val = 0.0;
    double loss_val = 0.0;
    double gw_val = 0.0;
    double dl_speed = 0.0;
    double ul_speed = 0.0;
    int wifi_sig = 0;
    int wifi_dbm = 0;
    bool has_data = false;
    bool is_conn = false;
    bool is_wf = false;
    std::string cur_ssid;
    std::string cur_ip;
    std::string cur_iface;
    std::string cur_gw;
    std::string cur_ping_target;
    std::string cur_ping_label;
    {
        std::lock_guard<std::mutex> lock(m_metrics_mutex);
        ping_val = m_ping;
        jitter_val = m_jitter;
        loss_val = m_packet_loss;
        gw_val = m_gateway_ping;
        dl_speed = m_download_speed;
        ul_speed = m_upload_speed;
        wifi_sig = m_wifi_signal_pct;
        wifi_dbm = m_wifi_signal_dbm;
        has_data = m_has_ping_data;
        is_conn = m_connected;
        is_wf = m_is_wifi;
        cur_ssid = m_ssid;
        cur_ip = m_ip;
        cur_iface = m_interface;
        cur_gw = m_gateway_ip;
        cur_ping_target = m_ping_target;
        cur_ping_label = m_ping_label;
    }

    // 1. Title Row: Left (SSID / Status), Right (Wi-Fi Signal Badge)
    std::string title_left;
    ColorRGBA title_left_col = config.colors.accent_blue;
    if (is_conn) {
        if (is_wf) {
            title_left = "󰖩 " + (cur_ssid.empty() ? "Wi-Fi" : cur_ssid);
        } else {
            title_left = "󰈀 Ethernet Terhubung";
        }
    } else {
        title_left = "󰖪 Terputus";
        title_left_col = config.colors.danger;
    }

    PangoFontDescription* title_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(title_desc, 10 * PANGO_SCALE);
    pango_font_description_set_weight(title_desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, title_desc);
    pango_layout_set_width(layout, 220 * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(layout, title_left.c_str(), -1);
    cairo_set_source_rgba(cr, title_left_col.r, title_left_col.g, title_left_col.b, 1.0);
    cairo_move_to(cr, 16, 10);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(title_desc);

    // Title Right: Signal strength badge (if Wi-Fi)
    if (is_conn && is_wf && wifi_sig > 0) {
        PangoFontDescription* sig_desc = pango_font_description_copy(font_desc);
        pango_font_description_set_size(sig_desc, 9 * PANGO_SCALE);
        pango_font_description_set_weight(sig_desc, PANGO_WEIGHT_BOLD);
        pango_layout_set_font_description(layout, sig_desc);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

        std::string sig_text = "󰤨 " + std::to_string(wifi_sig) + "% (" + std::to_string(wifi_dbm) + " dBm)";
        pango_layout_set_text(layout, sig_text.c_str(), -1);
        int stw, sth;
        pango_layout_get_pixel_size(layout, &stw, &sth);

        ColorRGBA sig_col = (wifi_sig >= 70) ? config.colors.accent : ((wifi_sig >= 40) ? config.colors.warning : config.colors.danger);
        cairo_set_source_rgba(cr, sig_col.r, sig_col.g, sig_col.b, 1.0);
        cairo_move_to(cr, w - 16 - stw, 11);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(sig_desc);
    }

    // 2. Subtitle Row: Left (Local IP & Interface), Right (Gateway IP)
    PangoFontDescription* sub_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(sub_desc, 8.5 * PANGO_SCALE);
    pango_layout_set_font_description(layout, sub_desc);

    if (is_conn) {
        std::string ip_left = "󰩟 " + cur_ip + " (" + (cur_iface.empty() ? "wlan0" : cur_iface) + ")";
        pango_layout_set_width(layout, 220 * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_text(layout, ip_left.c_str(), -1);
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
        cairo_move_to(cr, 16, 30);
        pango_cairo_show_layout(cr, layout);

        if (!cur_gw.empty()) {
            std::string gw_right = "󰌘 GW: " + cur_gw;
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
            pango_layout_set_text(layout, gw_right.c_str(), -1);
            int gtw, gth;
            pango_layout_get_pixel_size(layout, &gtw, &gth);
            cairo_move_to(cr, w - 16 - gtw, 30);
            pango_cairo_show_layout(cr, layout);
        }
    } else {
        pango_layout_set_width(layout, (w - 32) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_text(layout, "Tidak ada koneksi internet aktif", -1);
        cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
        cairo_move_to(cr, 16, 30);
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(sub_desc);

    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    auto draw_rounded_rect = [&](double rx, double ry, double rw, double rh, double radius) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, rx + radius, ry + radius, radius, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, rx + rw - radius, ry + radius, radius, -M_PI / 2, 0);
        cairo_arc(cr, rx + rw - radius, ry + rh - radius, radius, 0, M_PI / 2);
        cairo_arc(cr, rx + radius, ry + rh - radius, radius, M_PI / 2, M_PI);
        cairo_close_path(cr);
    };

    struct CardInfo {
        std::string label;
        std::string value;
        ColorRGBA color;
    };

    // Metric 1: Ping
    std::string ping_str;
    ColorRGBA ping_col;
    if (!m_connected || !has_data) {
        ping_str = m_connected ? "..." : "--";
        ping_col = config.colors.subtext;
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(ping_val >= 100.0 ? 0 : 1) << ping_val << " ms";
        ping_str = ss.str();
        if (ping_val < 60.0) ping_col = config.colors.accent;
        else if (ping_val < 150.0) ping_col = config.colors.warning;
        else ping_col = config.colors.danger;
    }

    // Metric 2: Jitter
    std::string jitter_str;
    ColorRGBA jitter_col;
    if (!m_connected || !has_data) {
        jitter_str = m_connected ? "..." : "--";
        jitter_col = config.colors.subtext;
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << jitter_val << " ms";
        jitter_str = ss.str();
        if (jitter_val < 10.0) jitter_col = config.colors.accent;
        else if (jitter_val < 30.0) jitter_col = config.colors.warning;
        else jitter_col = config.colors.danger;
    }

    // Metric 3: Packet Loss
    std::string loss_str;
    ColorRGBA loss_col;
    if (!m_connected) {
        loss_str = "100%";
        loss_col = config.colors.danger;
    } else if (!has_data) {
        loss_str = "...";
        loss_col = config.colors.subtext;
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(loss_val == 0.0 ? 0 : 1) << loss_val << "%";
        loss_str = ss.str();
        if (loss_val == 0.0) loss_col = config.colors.accent;
        else if (loss_val <= 5.0) loss_col = config.colors.warning;
        else loss_col = config.colors.danger;
    }

    // Metric 4: Download Speed
    std::string dl_str = m_connected ? format_speed(dl_speed) : "--";
    ColorRGBA dl_col = config.colors.accent_blue;

    // Metric 5: Upload Speed
    std::string ul_str = m_connected ? format_speed(ul_speed) : "--";
    ColorRGBA ul_col = config.colors.accent_purple;

    // Metric 6: Gateway Ping
    std::string gw_str;
    ColorRGBA gw_col;
    if (!m_connected || gw_val < 0.0) {
        gw_str = "--";
        gw_col = config.colors.danger;
    } else if (gw_val == 0.0) {
        gw_str = m_connected ? "..." : "--";
        gw_col = config.colors.subtext;
    } else {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << gw_val << " ms";
        gw_str = ss.str();
        if (gw_val < 10.0) gw_col = config.colors.accent;
        else if (gw_val < 25.0) gw_col = config.colors.warning;
        else gw_col = config.colors.danger;
    }

    std::vector<CardInfo> row1 = {
        {"󰅒 PING", ping_str, ping_col},
        {"󱥸 JITTER", jitter_str, jitter_col},
        {"󰓧 LOSS", loss_str, loss_col}
    };

    std::vector<CardInfo> row2 = {
        {"󰇚 DOWNLOAD", dl_str, dl_col},
        {"󰕒 UPLOAD", ul_str, ul_col},
        {"󰌘 GATEWAY", gw_str, gw_col}
    };

    int card_h = 42;
    int pad_x = 16;
    int gap = 8;
    int total_card_w = w - (2 * pad_x) - (2 * gap);
    int card_w = total_card_w / 3;

    PangoFontDescription* label_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(label_desc, 8 * PANGO_SCALE);

    PangoFontDescription* val_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(val_desc, 10.5 * PANGO_SCALE);
    pango_font_description_set_weight(val_desc, PANGO_WEIGHT_BOLD);

    auto render_card_row = [&](const std::vector<CardInfo>& row_cards, int row_y) {
        for (size_t i = 0; i < row_cards.size(); ++i) {
            int cur_x = pad_x + i * (card_w + gap);
            int cur_w = (i == row_cards.size() - 1) ? (w - pad_x - cur_x) : card_w;

            draw_rounded_rect(cur_x, row_y, cur_w, card_h, 8.0);
            cairo_set_source_rgba(cr, config.colors.card_bg.r, config.colors.card_bg.g, config.colors.card_bg.b, 0.85);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, config.colors.border.r, config.colors.border.g, config.colors.border.b, 0.45);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            pango_layout_set_font_description(layout, label_desc);
            pango_layout_set_text(layout, row_cards[i].label.c_str(), -1);
            int ltw, lth;
            pango_layout_get_pixel_size(layout, &ltw, &lth);
            cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.85);
            cairo_move_to(cr, cur_x + (cur_w - ltw) / 2, row_y + 4);
            pango_cairo_show_layout(cr, layout);

            pango_layout_set_font_description(layout, val_desc);
            pango_layout_set_text(layout, row_cards[i].value.c_str(), -1);
            int vtw, vth;
            pango_layout_get_pixel_size(layout, &vtw, &vth);
            cairo_set_source_rgba(cr, row_cards[i].color.r, row_cards[i].color.g, row_cards[i].color.b, 1.0);
            cairo_move_to(cr, cur_x + (cur_w - vtw) / 2, row_y + 19);
            pango_cairo_show_layout(cr, layout);
        }
    };

    render_card_row(row1, 52);
    render_card_row(row2, 100);

    pango_font_description_free(label_desc);
    pango_font_description_free(val_desc);

    // 4. Subtle footer target info (No truncation)
    PangoFontDescription* footer_desc = pango_font_description_copy(font_desc);
    pango_font_description_set_size(footer_desc, 8 * PANGO_SCALE);
    pango_layout_set_font_description(layout, footer_desc);
    std::string footer_str = "● Target: " + (cur_ping_target.empty() ? "1.1.1.1" : cur_ping_target) + " (" + (cur_ping_label.empty() ? "Cloudflare" : cur_ping_label) + ") • Real-Time";
    pango_layout_set_text(layout, footer_str.c_str(), -1);
    int ftw, fth;
    pango_layout_get_pixel_size(layout, &ftw, &fth);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.6);
    cairo_move_to(cr, (w - ftw) / 2, 149);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(footer_desc);

    g_object_unref(layout);

    // Clicking anywhere in expanded network closes it
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
