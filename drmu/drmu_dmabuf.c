#include "drmu_dmabuf.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/mman.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "drmu.h"
#include "drmu_fmts.h"
#include "drmu_log.h"

struct drmu_dmabuf_env_s {
    atomic_int ref_count;
    drmu_env_t * du;
    int fd;
    size_t page_size;
};

drmu_fb_t *
drmu_dmabuf_fb_new_mod(drmu_dmabuf_env_t * const dde, const uint32_t w, const uint32_t h, const uint32_t format, const uint64_t mod)
{
    const drmu_fmt_info_t * const fmti = drmu_fmt_info_find_fmt(format);
    drmu_env_t * const du = dde->du;
    unsigned int i;
    unsigned int layers;
    unsigned int bypp;
    uint32_t w2 = (w + 15) & ~15;
    uint32_t h2 = (h + 15) & ~15;
    drmu_fb_t * fb;
    uint32_t offset = 0;

    if (fmti == NULL) {
        drmu_err(du, "%s: Format not found: %s", __func__, drmu_log_fourcc(format));
        return NULL;
    }

    if ((fb = drmu_fb_int_alloc(du)) == NULL)
        return NULL;

    drmu_fb_int_fmt_size_set(fb, format, w2, h2, drmu_rect_wh(w, h));

    layers = drmu_fmt_info_plane_count(fmti);
    bypp = (drmu_fmt_info_pixel_bits(fmti) + 7) / 8;

    for (offset = 0, i = 0; i != layers; ++i) {
        const uint32_t stride = w2 * bypp / drmu_fmt_info_wdiv(fmti, i);
        const uint32_t size = stride * h2 / drmu_fmt_info_hdiv(fmti, i);
        offset += size;
    }


    {
        struct dma_heap_allocation_data data = {
            .len = (offset + dde->page_size - 1) & ~(dde->page_size - 1),
            .fd = 0,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0
        };
        void * map_ptr;
        drmu_bo_t * bo;

        while (ioctl(dde->fd, DMA_HEAP_IOCTL_ALLOC, &data)) {
            const int err = errno;
            if (err == EINTR)
                continue;
            drmu_err(dde->du, "Failed to alloc %" PRIu64 " from dma-heap(fd=%d): %d (%s)",
                    (uint64_t)data.len, dde->fd, err, strerror(err));
            goto fail;
        }

        drmu_fb_int_fd_set(fb, data.fd);

        if ((bo = drmu_bo_new_fd(du, data.fd)) == NULL) {
            drmu_err(du, "%s: Failed to allocate BO", __func__);
            goto fail;
        }

        drmu_fb_int_bo_set(fb, 0, bo);

        if ((map_ptr = mmap(NULL, (size_t)data.len,
                            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                            data.fd, 0)) == MAP_FAILED) {
            drmu_err(du, "%s: mmap failed (size=%zd, fd=%d): %s", __func__,
                     (size_t)data.len, data.fd, strerror(errno));
            goto fail;
        }

        drmu_fb_int_mmap_set(fb, map_ptr, (size_t)data.len, w2 * bypp);
    }

    for (offset = 0, i = 0; i != layers; ++i) {
        const uint32_t stride = w2 * bypp / drmu_fmt_info_wdiv(fmti, i);
        const uint32_t size = stride * h2 / drmu_fmt_info_hdiv(fmti, i);
        drmu_fb_int_layer_mod_set(fb, i, 0, stride, offset, mod);
        offset += size;
    }

    if (drmu_fb_int_make(fb))
        goto fail;

    return fb;

fail:
    drmu_fb_int_free(fb);
    return NULL;
}

drmu_dmabuf_env_t *
drmu_dmabuf_ref(drmu_dmabuf_env_t * const dde)
{
    atomic_fetch_add(&dde->ref_count, 1);
    return dde;
}

void drmu_dmabuf_unref(drmu_dmabuf_env_t ** const ppdde)
{
    drmu_dmabuf_env_t * const dde = *ppdde;
    if (dde == NULL)
        return;
    *ppdde = NULL;
    if (atomic_fetch_sub(&dde->ref_count, 1) != 0)
        return;

    drmu_env_unref(&dde->du);
    if (dde->fd != -1)
        close(dde->fd);
    free(dde);
}

drmu_dmabuf_env_t *
drmu_dmabuf_env_new_fd(struct drmu_env_s * const du, const int fd)
{
    drmu_dmabuf_env_t * const dde = calloc(1, sizeof(*dde));
    if (dde == NULL) {
        close(fd);
        return NULL;
    }
    dde->du = drmu_env_ref(du);
    dde->fd = fd;
    dde->page_size = (size_t)sysconf(_SC_PAGE_SIZE);

    return dde;
}

