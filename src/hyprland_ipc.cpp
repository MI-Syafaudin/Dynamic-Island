#include "hyprland_ipc.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace di {

HyprlandIpc::HyprlandIpc() {
    connect();
}

HyprlandIpc::~HyprlandIpc() {
    disconnect();
}

std::string HyprlandIpc::get_socket_path() {
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    const char* his = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg_runtime || !his) return "";
    return std::string(xdg_runtime) + "/hypr/" + his + "/.socket2.sock";
}

bool HyprlandIpc::connect() {
    disconnect();
    std::string path = get_socket_path();
    if (path.empty()) return false;

    m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_fd < 0) return false;

    // Set non-blocking
    int flags = fcntl(m_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(m_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(m_fd);
            m_fd = -1;
            return false;
        }
    }

    return true;
}

void HyprlandIpc::disconnect() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    m_buffer.clear();
}

void HyprlandIpc::process_incoming() {
    if (m_fd < 0) {
        connect();
        return;
    }

    char buf[1024];
    while (true) {
        ssize_t n = read(m_fd, buf, sizeof(buf));
        if (n > 0) {
            m_buffer.append(buf, n);
        } else {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                // Socket disconnected
                disconnect();
                break;
            }
        }
    }

    // Process complete lines
    size_t pos;
    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        std::string line = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 1);

        if (line.empty()) continue;

        size_t delim = line.find(">>");
        if (delim != std::string::npos) {
            std::string event = line.substr(0, delim);
            std::string data = line.substr(delim + 2);

            if (event == "workspace" && m_on_workspace) {
                try {
                    int ws = std::stoi(data);
                    m_on_workspace(ws);
                } catch (...) {}
            } else if (event == "activewindow" && m_on_window) {
                size_t comma = data.find(',');
                if (comma != std::string::npos) {
                    std::string app_class = data.substr(0, comma);
                    std::string app_title = data.substr(comma + 1);
                    m_on_window(app_class, app_title);
                } else {
                    m_on_window("", "");
                }
            } else if (event == "fullscreen" && m_on_fullscreen) {
                bool is_fs = (data == "1");
                m_on_fullscreen(is_fs);
            }
        }
    }
}

} // namespace di
