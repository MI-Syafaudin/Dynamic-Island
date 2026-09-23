#define _GNU_SOURCE
#include "wayland.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <linux/input-event-codes.h>

namespace di {

// Buffer listener
static void buffer_release(void* data, struct wl_buffer* wl_buffer) {
    auto* buf = static_cast<WaylandBuffer*>(data);
    if (buf) {
        buf->busy = false;
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

// Frame callback listener
static void frame_done(void* data, struct wl_callback* callback, uint32_t time) {
    auto* backend = static_cast<WaylandBackend*>(data);
    wl_callback_destroy(callback);
    if (backend) {
        backend->handle_frame_done();
    }
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

// Layer surface listeners
static void layer_surface_configure(void* data, struct zwlr_layer_surface_v1* surface,
                                   uint32_t serial, uint32_t width, uint32_t height) {
    auto* backend = static_cast<WaylandBackend*>(data);
    if (backend) {
        backend->handle_configure(serial, width, height);
    }
}

static void layer_surface_closed(void* data, struct zwlr_layer_surface_v1* surface) {
    // Handled on shutdown
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

// Pointer listeners
static void pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial,
                         struct wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
    auto* backend = static_cast<WaylandBackend*>(data);
    if (backend) {
        backend->handle_pointer_motion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }
}

static void pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface) {}

static void pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    auto* backend = static_cast<WaylandBackend*>(data);
    if (backend) {
        backend->handle_pointer_motion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }
}

static void pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial,
                          uint32_t time, uint32_t button, uint32_t state) {
    auto* backend = static_cast<WaylandBackend*>(data);
    if (backend) {
        // state: 1 = pressed, 0 = released
        backend->handle_pointer_button(0, 0, button, state);
    }
}

static void pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    auto* backend = static_cast<WaylandBackend*>(data);
    if (backend && axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        backend->handle_pointer_axis(wl_fixed_to_double(value));
    }
}

static void pointer_frame(void* data, struct wl_pointer* pointer) {}
static void pointer_axis_source(void* data, struct wl_pointer* pointer, uint32_t axis_source) {}
static void pointer_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

// Seat listener
static void seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities) {
    auto* backend = static_cast<WaylandBackend*>(data);
    // Bind pointer if available
}

static void seat_name(void* data, struct wl_seat* seat, const char* name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_handle_global(void* data, struct wl_registry* registry,
                                  uint32_t name, const char* interface, uint32_t version) {
    auto* b = static_cast<WaylandBackend*>(data);
    if (b) {
        b->handle_registry_global(registry, name, interface, version);
    }
}

static void registry_handle_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

WaylandBackend::WaylandBackend() {}

WaylandBackend::~WaylandBackend() {
    cleanup();
}

bool WaylandBackend::init(int width, int height, int margin_top) {
    m_width = width;
    m_height = height;
    m_margin_top = margin_top;

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::cerr << "Error: Could not connect to Wayland display." << std::endl;
        return false;
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registry_listener, this);
    wl_display_roundtrip(m_display);

    if (!m_compositor || !m_shm || !m_layer_shell) {
        std::cerr << "Error: Compositor does not support required Wayland interfaces." << std::endl;
        return false;
    }

    m_surface = wl_compositor_create_surface(m_compositor);
    if (!m_surface) {
        std::cerr << "Error: Failed to create Wayland surface." << std::endl;
        return false;
    }

    m_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        m_layer_shell, m_surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "dynamic-island"
    );
    if (!m_layer_surface) {
        std::cerr << "Error: Failed to create layer surface." << std::endl;
        return false;
    }

    zwlr_layer_surface_v1_add_listener(m_layer_surface, &layer_surface_listener, this);
    zwlr_layer_surface_v1_set_anchor(m_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
    zwlr_layer_surface_v1_set_margin(m_layer_surface, m_margin_top, 0, 0, 0);
    zwlr_layer_surface_v1_set_size(m_layer_surface, m_width, m_height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        m_layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer_surface, 0);

    wl_surface_commit(m_surface);
    wl_display_roundtrip(m_display);

    return true;
}

bool WaylandBackend::create_shm_buffer(WaylandBuffer& buf, int width, int height) {
    destroy_shm_buffer(buf);

    int stride = width * 4;
    size_t size = stride * height;

    int fd = memfd_create("dynamic-island-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return false;

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return false;
    }

    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(m_shm, fd, size);
    buf.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    buf.shm_data = data;
    buf.size = size;
    buf.busy = false;

    buf.cairo_surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, width, height, stride
    );

    wl_buffer_add_listener(buf.buffer, &buffer_listener, &buf);
    return true;
}

void WaylandBackend::destroy_shm_buffer(WaylandBuffer& buf) {
    if (buf.cairo_surface) {
        cairo_surface_destroy(buf.cairo_surface);
        buf.cairo_surface = nullptr;
    }
    if (buf.buffer) {
        wl_buffer_destroy(buf.buffer);
        buf.buffer = nullptr;
    }
    if (buf.shm_data && buf.shm_data != MAP_FAILED) {
        munmap(buf.shm_data, buf.size);
        buf.shm_data = nullptr;
    }
    buf.size = 0;
    buf.busy = false;
}

void WaylandBackend::cleanup() {
    destroy_shm_buffer(m_buffers[0]);
    destroy_shm_buffer(m_buffers[1]);

    if (m_frame_callback) {
        wl_callback_destroy(m_frame_callback);
        m_frame_callback = nullptr;
    }
    if (m_pointer) {
        wl_pointer_destroy(m_pointer);
        m_pointer = nullptr;
    }
    if (m_layer_surface) {
        zwlr_layer_surface_v1_destroy(m_layer_surface);
        m_layer_surface = nullptr;
    }
    if (m_surface) {
        wl_surface_destroy(m_surface);
        m_surface = nullptr;
    }
    if (m_layer_shell) {
        zwlr_layer_shell_v1_destroy(m_layer_shell);
        m_layer_shell = nullptr;
    }
    if (m_shm) {
        wl_shm_destroy(m_shm);
        m_shm = nullptr;
    }
    if (m_compositor) {
        wl_compositor_destroy(m_compositor);
        m_compositor = nullptr;
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
    }
    if (m_display) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }
}

void WaylandBackend::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_width == width && m_height == height) return;

    m_width = width;
    m_height = height;

    if (m_layer_surface) {
        zwlr_layer_surface_v1_set_size(m_layer_surface, m_width, m_height);
    }
}

void WaylandBackend::set_margin_top(int margin) {
    m_margin_top = margin;
    if (m_layer_surface) {
        zwlr_layer_surface_v1_set_margin(m_layer_surface, m_margin_top, 0, 0, 0);
    }
}

void WaylandBackend::set_visible(bool visible) {
    m_visible = visible;
    if (m_layer_surface) {
        if (visible) {
            zwlr_layer_surface_v1_set_layer(m_layer_surface, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
        } else {
            zwlr_layer_surface_v1_set_layer(m_layer_surface, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);
        }
        wl_surface_commit(m_surface);
    }
}

void WaylandBackend::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    (void)width; (void)height;
    zwlr_layer_surface_v1_ack_configure(m_layer_surface, serial);
    m_configured = true;
}

void WaylandBackend::handle_registry_global(struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    (void)version;
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        m_compositor = static_cast<struct wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        m_shm = static_cast<struct wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        m_layer_shell = static_cast<struct zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        m_seat = static_cast<struct wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 5));
        wl_seat_add_listener(m_seat, &seat_listener, this);
        m_pointer = wl_seat_get_pointer(m_seat);
        if (m_pointer) {
            wl_pointer_add_listener(m_pointer, &pointer_listener, this);
        }
    }
}

void WaylandBackend::handle_frame_done() {
    m_frame_callback = nullptr;
    if (m_on_frame) {
        m_on_frame();
    }
}

void WaylandBackend::request_frame_callback() {
    if (!m_frame_callback && m_surface) {
        m_frame_callback = wl_surface_frame(m_surface);
        wl_callback_add_listener(m_frame_callback, &frame_listener, this);
        wl_surface_commit(m_surface);
    }
}

cairo_t* WaylandBackend::begin_frame() {
    if (!m_configured || !m_visible || m_width <= 0 || m_height <= 0) return nullptr;

    // Pick next free buffer
    m_current_buffer_idx = (m_current_buffer_idx + 1) % 2;
    WaylandBuffer& buf = m_buffers[m_current_buffer_idx];

    // Recreate buffer if size changed
    int stride = m_width * 4;
    size_t required_size = stride * m_height;
    if (!buf.buffer || buf.size != required_size) {
        if (!create_shm_buffer(buf, m_width, m_height)) {
            return nullptr;
        }
    }

    buf.busy = true;
    m_active_cr = cairo_create(buf.cairo_surface);

    // Clear buffer to transparent
    cairo_save(m_active_cr);
    cairo_set_operator(m_active_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(m_active_cr);
    cairo_restore(m_active_cr);

    return m_active_cr;
}

void WaylandBackend::end_frame() {
    if (!m_active_cr) return;

    cairo_destroy(m_active_cr);
    m_active_cr = nullptr;

    WaylandBuffer& buf = m_buffers[m_current_buffer_idx];
    cairo_surface_flush(buf.cairo_surface);

    wl_surface_attach(m_surface, buf.buffer, 0, 0);
    wl_surface_damage(m_surface, 0, 0, m_width, m_height);
    wl_surface_commit(m_surface);
}

int WaylandBackend::get_display_fd() const {
    return m_display ? wl_display_get_fd(m_display) : -1;
}

bool WaylandBackend::prepare_read() {
    if (!m_display) return false;
    while (wl_display_prepare_read(m_display) != 0) {
        wl_display_dispatch_pending(m_display);
    }
    return true;
}

void WaylandBackend::cancel_read() {
    if (m_display) {
        wl_display_cancel_read(m_display);
    }
}

void WaylandBackend::read_events() {
    if (m_display) {
        if (wl_display_read_events(m_display) == 0) {
            wl_display_dispatch_pending(m_display);
        }
    }
}

void WaylandBackend::dispatch_pending() {
    if (m_display) {
        wl_display_dispatch_pending(m_display);
    }
}

void WaylandBackend::flush() {
    if (m_display) {
        wl_display_flush(m_display);
    }
}

void WaylandBackend::handle_pointer_motion(double x, double y) {
    m_pointer_x = x;
    m_pointer_y = y;
    if (m_on_motion) {
        m_on_motion(x, y);
    }
}

void WaylandBackend::handle_pointer_button(double x, double y, uint32_t button, uint32_t state) {
    if (m_on_button) {
        m_on_button(m_pointer_x, m_pointer_y, button, (state == 1));
    }
}

void WaylandBackend::handle_pointer_axis(double value) {
    if (m_on_scroll) {
        m_on_scroll(value);
    }
}

} // namespace di
