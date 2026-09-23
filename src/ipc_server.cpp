#include "ipc_server.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace di {

std::string IpcServer::get_socket_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) {
        return std::string(xdg) + "/dynamic_island.sock";
    }
    const char* user = std::getenv("USER");
    return "/tmp/dynamic_island_" + std::string(user ? user : "user") + ".sock";
}

IpcServer::IpcServer() {}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    stop();
    std::string path = get_socket_path();
    unlink(path.c_str());

    m_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) return false;

    // Set non-blocking
    int flags = fcntl(m_server_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_server_fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    if (listen(m_server_fd, 8) < 0) {
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    return true;
}

void IpcServer::stop() {
    if (m_server_fd >= 0) {
        close(m_server_fd);
        m_server_fd = -1;
        unlink(get_socket_path().c_str());
    }
}

void IpcServer::process_connections() {
    if (m_server_fd < 0) return;

    while (true) {
        int client_fd = accept(m_server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            break;
        }

        char buffer[1024];
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            std::string line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;
            std::vector<std::string> args;
            std::string arg;
            while (iss >> arg) {
                args.push_back(arg);
            }

            std::string response = "OK\n";
            if (m_on_command) {
                response = m_on_command(cmd, args) + "\n";
            }
            write(client_fd, response.c_str(), response.length());
        }
        close(client_fd);
    }
}

bool IpcServer::send_command(const std::string& cmd_line, std::string& response) {
    std::string path = get_socket_path();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    std::string send_str = cmd_line + "\n";
    write(fd, send_str.c_str(), send_str.length());

    char buffer[1024];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        response = buffer;
    }
    close(fd);
    return true;
}

} // namespace di
