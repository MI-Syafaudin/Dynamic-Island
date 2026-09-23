#pragma once

#include <string>
#include <functional>

namespace di {

class HyprlandIpc {
public:
    using WorkspaceCallback = std::function<void(int)>;
    using WindowCallback = std::function<void(const std::string&, const std::string&)>;
    using FullscreenCallback = std::function<void(bool)>;

    HyprlandIpc();
    ~HyprlandIpc();

    bool connect();
    void disconnect();
    int get_fd() const { return m_fd; }

    void set_workspace_callback(WorkspaceCallback cb) { m_on_workspace = cb; }
    void set_window_callback(WindowCallback cb) { m_on_window = cb; }
    void set_fullscreen_callback(FullscreenCallback cb) { m_on_fullscreen = cb; }

    void process_incoming();

private:
    int m_fd = -1;
    std::string m_buffer;

    WorkspaceCallback m_on_workspace;
    WindowCallback m_on_window;
    FullscreenCallback m_on_fullscreen;

    std::string get_socket_path();
};

} // namespace di
