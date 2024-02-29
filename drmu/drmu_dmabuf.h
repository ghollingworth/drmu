#ifndef _DRMU_DRMU_DMABUF_H
#define _DRMU_DRMU_DMABUF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_env_s;
struct drmu_fb_s;
struct drmu_pool_s;

struct drmu_dmabuf_env_s;
typedef struct drmu_dmabuf_env_s drmu_dmabuf_env_t;

struct drmu_fb_s * drmu_fb_new_dmabuf_mod(drmu_dmabuf_env_t * const dde, const uint32_t w, const uint32_t h, const uint32_t format, const uint64_t mod);

drmu_dmabuf_env_t * drmu_dmabuf_env_ref(drmu_dmabuf_env_t * const dde);
void drmu_dmabuf_env_unref(drmu_dmabuf_env_t ** const ppdde);
// Takes control of fd and will close it when the env is deleted
// or on creation error
drmu_dmabuf_env_t * drmu_dmabuf_env_new_fd(struct drmu_env_s * const du, int fd);

drmu_dmabuf_env_t * drmu_dmabuf_env_new_video(struct drmu_env_s * const du);

struct drmu_pool_s * drmu_pool_new_dmabuf(drmu_dmabuf_env_t * dde, unsigned int total_fbs_max);

#ifdef __cplusplus
}
#endif

#endif