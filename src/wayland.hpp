#pragma once

#include <wayland-client.h>
#include <cairo/cairo.h>
#include <string>
#include <functional>
#include "protocols/wlr-layer-shell-protocol.h"
#include "protocols/xdg-shell-protocol.h"

namespace di {

struct WaylandBuffer {
    struct wl_buffer* buffer = nullptr;
    void* shm_data = nullptr;
    size_t size = 0;
    bool busy = false;
    cairo_surface_t* cairo_surface = nullptr;
};

class WaylandBackend {
public:
    using MouseButtonCallback = std::function<void(double, double, uint32_t, bool)>;
    using MouseMotionCallback = std::function<void(double, double)>;
    using MouseScrollCallback = std::function<void(double)>;
    using FrameCallback = std::function<void()>;

    WaylandBackend();
    ~WaylandBackend();

    bool init(int width, int height, int margin_top, const std::string& layer = "overlay");
    void cleanup();

    void resize(int width, int height);
    void set_margin_top(int margin);
    void set_visible(bool visible);

    cairo_t* begin_frame();
    void end_frame();

    void request_frame_callback();

    int get_display_fd() const;
    bool prepare_read();
    void cancel_read();
    void read_events();
    void dispatch_pending();
    void flush();

    void set_mouse_button_callback(MouseButtonCallback cb) { m_on_button = cb; }
    void set_mouse_motion_callback(MouseMotionCallback cb) { m_on_motion = cb; }
    void set_mouse_scroll_callback(MouseScrollCallback cb) { m_on_scroll = cb; }
    void set_frame_callback(FrameCallback cb) { m_on_frame = cb; }

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }

    // Internal callbacks
    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    void handle_frame_done();
    void handle_pointer_button(double x, double y, uint32_t button, uint32_t state);
    void handle_pointer_motion(double x, double y);
    void handle_pointer_axis(double value);
    void handle_registry_global(struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);

private:
    struct wl_display* m_display = nullptr;
    struct wl_registry* m_registry = nullptr;
    struct wl_compositor* m_compositor = nullptr;
    struct wl_shm* m_shm = nullptr;
    struct zwlr_layer_shell_v1* m_layer_shell = nullptr;
    struct wl_seat* m_seat = nullptr;
    struct wl_pointer* m_pointer = nullptr;
    struct wl_surface* m_surface = nullptr;
    struct zwlr_layer_surface_v1* m_layer_surface = nullptr;
    struct wl_callback* m_frame_callback = nullptr;

    int m_width = 260;
    int m_height = 34;
    int m_margin_top = 8;
    std::string m_layer = "overlay";
    uint32_t m_layer_shell_version = 1;
    bool m_configured = false;
    bool m_visible = true;

    uint32_t get_layer_enum() const;

    WaylandBuffer m_buffers[2];
    int m_current_buffer_idx = 0;
    cairo_t* m_active_cr = nullptr;

    double m_pointer_x = 0;
    double m_pointer_y = 0;

    MouseButtonCallback m_on_button;
    MouseMotionCallback m_on_motion;
    MouseScrollCallback m_on_scroll;
    FrameCallback m_on_frame;

    bool create_shm_buffer(WaylandBuffer& buf, int width, int height);
    void destroy_shm_buffer(WaylandBuffer& buf);
};

} // namespace di
