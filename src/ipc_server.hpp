#pragma once

#include <string>
#include <functional>
#include <vector>

namespace di {

class IpcServer {
public:
    using CommandCallback = std::function<std::string(const std::string&, const std::vector<std::string>&)>;

    IpcServer();
    ~IpcServer();

    bool start();
    void stop();
    int get_fd() const { return m_server_fd; }

    void set_command_callback(CommandCallback cb) { m_on_command = cb; }
    void process_connections();

    static std::string get_socket_path();
    static bool send_command(const std::string& cmd_line, std::string& response);

private:
    int m_server_fd = -1;
    CommandCallback m_on_command;
};

} // namespace di
