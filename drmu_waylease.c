#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "drmu.h"
#include "drmu_log.h"

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>

#include "drm-lease-v1-client-protocol.h"

struct waylease_env_s {
    const struct drmu_log_env_s * log;
    struct wp_drm_lease_device_v1 * drm_lease;
};

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version)
{
    struct waylease_env_s * const we = data;
    (void)version;

    drmu_debug_log(we->log, "Got interface '%s'", interface);
    if (strcmp(interface, wp_drm_lease_v1_interface.name) == 0)
        we->drm_lease = wl_registry_bind(registry, id, &wp_drm_lease_v1_interface, 1);
}

static void global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    (void)id;
    (void)data;
    (void)registry;
}

static const struct wl_registry_listener listener = {
    global_registry_handler,
    global_registry_remover
};

drmu_env_t * drmu_env_new_waylease(const struct drmu_log_env_s * const log2)
{
    const struct drmu_log_env_s * const log = (log2 == NULL) ? &drmu_log_env_none : log2;
    struct waylease_env_s * we = calloc(1, sizeof(*we));
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;

    if (!we)
        return NULL;
    we->log = log;

    display = wl_display_connect(NULL);
    if (display == NULL) {
        drmu_debug_log(log, "Can't connect to wayland display");
        goto fail;
    }
    drmu_debug_log(log, "Got display");

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, we);

    // This call the attached listener global_registry_handler
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

fail:
    free(we);
    return NULL;
}

