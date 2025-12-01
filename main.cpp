// ============================================================================
// Wayland Client — Server-Side Decorations (SSD)
// Janela simples usando a barra de título padrão do compositor.
// Não usa CSD, subsurfaces ou titlebars customizados.
// ============================================================================

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------
static struct wl_display* display;
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct wl_seat* seat;

static struct xdg_wm_base* wm_base;
static struct xdg_surface* xsurf;
static struct xdg_toplevel* toplevel;

static struct zxdg_decoration_manager_v1* deco_manager;
static struct zxdg_toplevel_decoration_v1* decoration;

static struct wl_surface* surface;

static int running = 1;
static int win_w = 800, win_h = 600;

// -----------------------------------------------------------------------------
// SHM BUFFER
// -----------------------------------------------------------------------------
typedef struct {
    int w, h;
    int stride, size;
    int fd;
    uint32_t* data;
    struct wl_buffer* buffer;
} Buffer;

static Buffer buffer;

static int create_shm_file(size_t size) {
    int fd = memfd_create("shm", MFD_CLOEXEC);
    ftruncate(fd, size);
    return fd;
}

static void buffer_create(Buffer* b, int w, int h) {
    b->w = w;
    b->h = h;
    b->stride = w * 4;
    b->size = b->stride * h;

    b->fd = create_shm_file(b->size);
    b->data =
        (uint32_t*)mmap(nullptr, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, b->fd, b->size);
    b->buffer = wl_shm_pool_create_buffer(
        pool, 0, w, h, b->stride, WL_SHM_FORMAT_ARGB8888
    );
    wl_shm_pool_destroy(pool);
}

static void draw(Buffer* b) {
    for (int i = 0; i < b->w * b->h; i++)
        b->data[i] = 0xff282828;  // fundo simples cinza escuro
}

// -----------------------------------------------------------------------------
// LISTENERS
// -----------------------------------------------------------------------------

// xdg_wm_base ping/pong
static void wm_base_ping(void* data, struct xdg_wm_base* wm, uint32_t serial) {
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping
};

// xdg_surface
static void xsurf_configure(void* data, struct xdg_surface* surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);

    draw(&buffer);

    wl_surface_attach(::surface, buffer.buffer, 0, 0);
    wl_surface_damage(::surface, 0, 0, buffer.w, buffer.h);
    wl_surface_commit(::surface);
}

static const struct xdg_surface_listener xsurf_listener = {
    xsurf_configure
};

// xdg_toplevel
static void toplevel_configure(void* data, struct xdg_toplevel* t,
                               int32_t w, int32_t h,
                               struct wl_array* states) {

    if (w > 0 && h > 0) {
        win_w = w;
        win_h = h;
        buffer_create(&buffer, win_w, win_h);
    }
}

static void toplevel_close(void* data, struct xdg_toplevel* t) {
    running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure, // configure
    .close = toplevel_close,      // close
    .configure_bounds = nullptr,            // configure_bounds
};

// seat
static void seat_capabilities(void* data, struct wl_seat* s, uint32_t caps) {}
static void seat_name(void* data, struct wl_seat* s, const char* name) {}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name
};

// registry
static void registry_global(void* data, struct wl_registry* reg,
                            uint32_t id, const char* iface, uint32_t ver) {
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        compositor = (wl_compositor*)wl_registry_bind(reg, id, &wl_compositor_interface, 4);

    else if (strcmp(iface, wl_shm_interface.name) == 0)
        shm = (wl_shm*)wl_registry_bind(reg, id, &wl_shm_interface, 1);

    else if (strcmp(iface, wl_seat_interface.name) == 0) {
        seat = (wl_seat*)wl_registry_bind(reg, id, &wl_seat_interface, 7);
        wl_seat_add_listener(seat, &seat_listener, nullptr);
    }

    else if (strcmp(iface, xdg_wm_base_interface.name) == 0)
        wm_base = (struct xdg_wm_base*)wl_registry_bind(reg, id, &xdg_wm_base_interface, 2);

    else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0)
        deco_manager = (struct zxdg_decoration_manager_v1*)wl_registry_bind(reg, id, &zxdg_decoration_manager_v1_interface, 1);
}

static void registry_remove(void* data, struct wl_registry* reg, uint32_t id) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove
};

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main() {
    display = wl_display_connect(nullptr);
    if (!display) return 1;

    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, nullptr);
    wl_display_roundtrip(display);

    // xdg_wm_base
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, nullptr);

    // surface + xdg_surface + toplevel
    surface = wl_compositor_create_surface(compositor);
    xsurf = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xsurf, &xsurf_listener, nullptr);

    toplevel = xdg_surface_get_toplevel(xsurf);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, nullptr);
    xdg_toplevel_set_title(toplevel, "SSD Window (Wayland)");

    // pedir decorações do servidor
    if (deco_manager) {
        decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(deco_manager, toplevel);

        zxdg_toplevel_decoration_v1_set_mode(
            decoration,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
        );
    }

    // buffer inicial
    buffer_create(&buffer, win_w, win_h);

    wl_surface_commit(surface);

    // loop principal
    while (running)
        wl_display_dispatch(display);

    wl_display_disconnect(display);
    return 0;
}