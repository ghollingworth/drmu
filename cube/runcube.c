#include "runcube.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "drm-common.h"

static void *
cube_thread(void * v)
{
    struct drmu_output_s * const dout = v;
    uint32_t format = DRM_FORMAT_ARGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    const struct egl *egl;
    const struct gbm *gbm;
    const struct drm *drm;

    (void)v;

    drm = init_drmu_dout(dout, 1000, format);
    gbm = init_gbm_drmu(drm->du, drm->mode->hdisplay, drm->mode->vdisplay, format, modifier);
    egl = init_cube_smooth(gbm, 0);
    drm->run(gbm, egl);

    return NULL;
}

int
runcube_drmu(struct drmu_output_s * dout)
{
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, cube_thread, dout);
    return 0;
}

