#include "network_module.hpp"
#include <cstdio>
#include <cstdlib>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>

namespace di {

NetworkModule::NetworkModule() {
    query_network();
}

void NetworkModule::query_local_ip() {
    m_ip = "127.0.0.1";
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        char host[NI_MAXHOST];
        int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
        if (s == 0) {
            m_ip = host;
            m_interface = ifa->ifa_name;
            break;
        }
    }
    freeifaddrs(ifaddr);
}

void NetworkModule::query_network() {
    FILE* fp = popen("nmcli -t -f TYPE,STATE,CONNECTION device 2>/dev/null", "r");
    if (!fp) return;

    char buffer[256];
    m_connected = false;
    m_is_wifi = false;
    m_ssid.clear();

    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

        // format: type:state:connection
        if (line.find("wifi:connected:") == 0) {
            m_connected = true;
            m_is_wifi = true;
            m_ssid = line.substr(15);
            break;
        } else if (line.find("ethernet:connected:") == 0) {
            m_connected = true;
            m_is_wifi = false;
            m_ssid = "Ethernet";
            break;
        }
    }
    pclose(fp);

    if (m_connected) {
        query_local_ip();
    }
}

void NetworkModule::update() {
    query_network();
}

void NetworkModule::draw_compact(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int& current_x, int y, int h, std::vector<HitBox>& hitboxes) {
    std::string icon = m_connected ? (m_is_wifi ? "󰖩" : "󰈀") : "󰖪";

    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    pango_layout_set_text(layout, icon.c_str(), -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    if (m_connected) {
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

void NetworkModule::draw_expanded(cairo_t* cr, PangoFontDescription* font_desc, const Config& config, int w, int h, std::vector<HitBox>& hitboxes) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);

    // Title
    std::string title = (m_connected ? (m_is_wifi ? "󰖩 WiFi Connected" : "󰈀 Ethernet Connected") : "󰖪 Disconnected");
    pango_layout_set_text(layout, title.c_str(), -1);
    cairo_set_source_rgba(cr, m_connected ? config.colors.accent_blue.r : config.colors.danger.r,
                              m_connected ? config.colors.accent_blue.g : config.colors.danger.g,
                              m_connected ? config.colors.accent_blue.b : config.colors.danger.b, 1.0);
    cairo_move_to(cr, 20, 10);
    pango_cairo_show_layout(cr, layout);

    // SSID / Network Name
    std::string net_str = "Network: " + (m_ssid.empty() ? "None" : m_ssid);
    pango_layout_set_text(layout, net_str.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.text.r, config.colors.text.g, config.colors.text.b, 1.0);
    cairo_move_to(cr, 20, 30);
    pango_cairo_show_layout(cr, layout);

    // IP Address & Interface
    std::string ip_str = "IP: " + m_ip + " (" + (m_interface.empty() ? "wlan0" : m_interface) + ")";
    pango_layout_set_text(layout, ip_str.c_str(), -1);
    cairo_set_source_rgba(cr, config.colors.subtext.r, config.colors.subtext.g, config.colors.subtext.b, 0.9);
    cairo_move_to(cr, 20, 50);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);

    // Clicking anywhere in expanded network closes it
    hitboxes.push_back({0, 0, w, h, "collapse", ""});
}

} // namespace di
