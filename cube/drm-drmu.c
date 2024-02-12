#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "drm-common.h"

#include "drmu.h"
#include "drmu_gbm.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_scan.h"

static struct drm drm_static = {.fd = -1};

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);

    (void)v;
    (void)level;

    if (n >= 255)
        n = 255;
    buf[n] = '\n';
    fwrite(buf, n + 1, 1, stderr);
}

static int run_drmu(const struct gbm *gbm, const struct egl *egl)
{
    struct drm * const drm = &drm_static;
    drmu_fb_t * fbs[NUM_BUFFERS];
    unsigned int i;
    unsigned int n;
	int64_t start_time, report_time, cur_time;

    for (i = 0; i != NUM_BUFFERS; ++i) {
        if ((fbs[i] = drmu_fb_gbm_attach(drm->du, gbm->bos[i])) == NULL) {
            fprintf(stderr, "Failed to attach gbm to drmu\n");
            return -1;
        }
    }

	start_time = report_time = get_time_ns();

	for (i = 0,  n = 0; i < drm->count; ++i, n = n >= NUM_BUFFERS - 1 ? 0 : n + 1) {

		/* Start fps measuring on second frame, to remove the time spent
		 * compiling shader, etc, from the fps:
		 */
		if (i == 1) {
			start_time = report_time = get_time_ns();
		}

		glBindFramebuffer(GL_FRAMEBUFFER, egl->fbs[n].fb);

		egl->draw(i);

		glFinish();

		/*
		 * Here you could also update drm plane layers if you want
		 * hw composition
		 */

        {
            drmu_atomic_t * da = drmu_atomic_new(drm->du);
            drmu_atomic_plane_add_fb(da, drm->dp, fbs[n], drmu_rect_wh(drm->mode->hdisplay, drm->mode->vdisplay));
            drmu_atomic_queue(&da);
        }
		drmu_env_queue_wait(drm->du);

		cur_time = get_time_ns();
		if (cur_time > (report_time + 2 * NSEC_PER_SEC)) {
			double elapsed_time = cur_time - start_time;
			double secs = elapsed_time / (double)NSEC_PER_SEC;
			unsigned frames = i - 1;  /* first frame ignored */
			printf("Rendered %u frames in %f sec (%f fps)\n",
				frames, secs, (double)frames/secs);
			report_time = cur_time;
		}

	}

	finish_perfcntrs();

	cur_time = get_time_ns();
	double elapsed_time = cur_time - start_time;
	double secs = elapsed_time / (double)NSEC_PER_SEC;
	unsigned frames = i - 1;  /* first frame ignored */
	printf("Rendered %u frames in %f sec (%f fps)\n",
		frames, secs, (double)frames/secs);

	dump_perfcntrs(frames, elapsed_time);

	return 0;
}

const struct drm *
init_drmu(const char *device, const char *mode_str, unsigned int count, const uint32_t format)
{
    struct drm * drm = &drm_static;
    const drmu_mode_simple_params_t * sparam;

    const drmu_log_env_t log = {
        .fn = drmu_log_stderr_cb,
        .v = NULL,
        .max_level = DRMU_LOG_LEVEL_INFO
    };

    (void)mode_str;

    drm->fd = -1;
    drm->count = count;
    drm->run = run_drmu;
    if ((drm->mode = calloc(1, sizeof(drm->mode))) == NULL) {
        fprintf(stderr, "Failed drm mode alloc\n");
        goto fail;
    }

    if (drmu_scan_output(device, &log, &drm->du, &drm->dout) != 0) {
        fprintf(stderr, "Failed drmu scan for device %s\n", device);
        goto fail;
    }
    drmu_env_restore_enable(drm->du);

    sparam = drmu_output_mode_simple_params(drm->dout);
    drm->mode->hdisplay = sparam->width;
    drm->mode->vdisplay = sparam->height;

    // This wants to be the primary
    if ((drm->dp = drmu_output_plane_ref_format(drm->dout, 0, drmu_gbm_fmt_to_drm(format), 0)) == NULL)
        goto fail;

    drm->fd = drmu_fd(drm->du);

    return drm;

fail:
    free(drm->mode);
    return NULL;
}

