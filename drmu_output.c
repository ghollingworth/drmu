#include "drmu_output.h"
#include "drmu_log.h"

#include <errno.h>
#include <string.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

// Update return value with a new one for cases where we don't stop on error
static inline int rvup(int rv1, int rv2)
{
    return rv2 ? rv2 : rv1;
}

struct drmu_output_s {
    drmu_env_t * du;
    drmu_crtc_t * dc;
    unsigned int conn_n;
    unsigned int conn_size;
    drmu_conn_t ** dns;
    bool max_bpc_allow;
    int mode_id;
    drmu_mode_simple_params_t mode_params;

    // These are expected to be static consts so no copy / no free
    const drmu_format_info_t * fmt_info;
    const char * colorspace;
    const char * broadcast_rgb;

    // HDR metadata
    drmu_isset_t hdr_metadata_isset;
    struct hdr_output_metadata hdr_metadata;
};

int
drmu_atomic_add_output_props(drmu_atomic_t * const da, drmu_output_t * const dout)
{
    int rv = 0;
    unsigned int i;

    for (i = 0; i != dout->conn_n; ++i) {
        drmu_conn_t * const dn = dout->dns[i];

        if (dout->fmt_info)
            rv = rvup(rv, drmu_atomic_conn_hi_bpc_set(da, dn, (drmu_format_info_bit_depth(dout->fmt_info) > 8)));
        if (dout->colorspace)
            rv = rvup(rv, drmu_atomic_conn_colorspace_set(da, dn, dout->colorspace));
        if (dout->broadcast_rgb)
            rv = rvup(rv, drmu_atomic_conn_broadcast_rgb_set(da, dn, dout->broadcast_rgb));
        if (dout->hdr_metadata_isset != DRMU_ISSET_UNSET)
            rv = rvup(rv, drmu_atomic_conn_hdr_metadata_set(da, dn,
                dout->hdr_metadata_isset == DRMU_ISSET_NULL ? NULL : &dout->hdr_metadata));
    }

    return rv;
}

// Set all the fb info props that might apply to a crtc on the crtc
// (e.g. hdr_metadata, colorspace) but do not set the mode (resolution
// and refresh)
int
drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb)
{
    drmu_isset_t hdr_isset = drmu_fb_hdr_metadata_isset(fb);
    const drmu_format_info_t * fmt_info = drmu_fb_format_info_get(fb);
    const char * colorspace             = drmu_fb_colorspace_get(fb);
    const char * broadcast_rgb          = drmu_color_range_to_broadcast_rgb(drmu_fb_color_range_get(fb));

    if (fmt_info)
        dout->fmt_info = fmt_info;
    if (colorspace)
        dout->colorspace = colorspace;
    if (broadcast_rgb)
        dout->broadcast_rgb = broadcast_rgb;

    if (hdr_isset != DRMU_ISSET_UNSET) {
        dout->hdr_metadata_isset = hdr_isset;
        if (hdr_isset == DRMU_ISSET_SET)
            dout->hdr_metadata = *drmu_fb_hdr_metadata_get(fb);
    }

    return 0;
}


int
drmu_output_mode_id_set(drmu_output_t * const dout, const int mode_id)
{
    if (mode_id != dout->mode_id) {
        drmu_mode_simple_params_t sp = drmu_conn_mode_simple_params(dout->dns[0], mode_id);
        if (sp.width == 0)
            return -EINVAL;

        dout->mode_id = mode_id;
        dout->mode_params = sp;
    }
    return 0;
}

const drmu_mode_simple_params_t *
drmu_output_mode_simple_params(const drmu_output_t * const dout)
{
    return &dout->mode_params;
}

int
drmu_mode_pick_simple_cb(void * v, const drmu_mode_simple_params_t * mode)
{
    const drmu_mode_simple_params_t * const p = v;

    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    const unsigned int r_m = mode->hz_x_1000;
    const unsigned int r_f = p->hz_x_1000;

    // We don't understand interlace
    if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0)
        return -1;

    if (p->width == mode->width && p->height == mode->height)
    {
        // If we haven't been given any hz then pick pref or fastest
        // Max out at 300Hz (=300,0000)
        if (r_f == 0)
            return pref ? 83000000 : 80000000 + (r_m >= 2999999 ? 2999999 : r_m);

        // Prefer a good match to 29.97 / 30 but allow the other
        if ((r_m + 10 >= r_f && r_m <= r_f + 10))
            return 100000000;
        if ((r_m + 100 >= r_f && r_m <= r_f + 100))
            return 95000000;
        // Double isn't bad
        if ((r_m + 10 >= r_f * 2 && r_m <= r_f * 2 + 10))
            return 90000000;
        if ((r_m + 100 >= r_f * 2 && r_m <= r_f * 2 + 100))
            return 85000000;
    }

    if (pref)
        return 50000000;

    return -1;
}

int
drmu_output_mode_pick_simple(drmu_output_t * const dout, drmu_mode_score_fn * const score_fn, void * const score_v)
{
    int best_score = -1;
    int best_mode = -1;
    int i;

    for (i = 0;; ++i) {
        const drmu_mode_simple_params_t sp = drmu_conn_mode_simple_params(dout->dns[0], i);
        int score;

        if (sp.width == 0)
            break;

        score = score_fn(score_v, &sp);
        if (score > best_score) {
            best_score = score;
            best_mode = i;
        }
    }

    return best_mode;
}

int
drmu_output_max_bpc_allow(drmu_output_t * const dout, const bool allow)
{
    dout->max_bpc_allow = allow;
    return 0;
}

int
drmu_output_add_output(drmu_output_t * const dout, const char * const conn_name)
{
    const size_t nlen = !conn_name ? 0 : strlen(conn_name);
    unsigned int i;
    drmu_env_t * const du = dout->du;
    drmu_conn_t * dn = NULL;
    drmu_conn_t * dn_t;
    drmu_crtc_t * dc_t = NULL;
    uint32_t crtc_id;

    for (i = 0; (dn_t = drmu_conn_find_n(du, i)) != NULL; ++i) {
        if (!drmu_conn_is_output(dn_t))
            continue;
        if (nlen && strncmp(conn_name, drmu_conn_name(dn_t), nlen) != 0)
            continue;
        // This prefers conns that are already attached to crtcs
        dn = dn_t;
        if ((crtc_id = drmu_conn_crtc_id_get(dn_t)) == 0 ||
            (dc_t = drmu_crtc_find_id(du, crtc_id)) == NULL)
            continue;
        break;
    }

    if (!dn)
        return -ENOENT;

    if (!dc_t) {
        drmu_warn(du, "Adding unattached conns NIF");
        return -EINVAL;
    }

    if (dout->conn_n >= dout->conn_size) {
        unsigned int n = !dout->conn_n ? 4 : dout->conn_n * 2;
        drmu_conn_t ** dns = realloc(dout->dns, sizeof(*dout->dns) * n);
        if (dns == NULL) {
            drmu_err(du, "Failed conn array realloc");
            return -ENOMEM;
        }
        dout->dns = dns;
        dout->conn_size = n;
    }

    dout->dns[dout->conn_n++] = dn;
    dout->dc = dc_t;

    return 0;
}

drmu_crtc_t *
drmu_output_crtc(const drmu_output_t * const dout)
{
    return !dout ? NULL : dout->dc;
}

drmu_conn_t *
drmu_output_conn(const drmu_output_t * const dout, const unsigned int n)
{
    return !dout || n >= dout->conn_n ? NULL : dout->dns[n];
}

static void
output_free(drmu_output_t * const dout)
{
    free(dout);
}

void
drmu_output_unref(drmu_output_t ** const ppdout)
{
    drmu_output_t * const dout = *ppdout;
    if (dout == NULL)
        return;
    *ppdout = NULL;

    output_free(dout);
}

drmu_output_t *
drmu_output_new(drmu_env_t * const du)
{
    drmu_output_t * const dout = calloc(1, sizeof(*dout));

    if (dout == NULL) {
        drmu_err(du, "Failed to alloc memory for drmu_output");
        return NULL;
    }

    dout->du = du;
    return dout;
}

