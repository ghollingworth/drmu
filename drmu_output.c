#include "drmu_output.h"
#include "drmu_log.h"

struct drmu_output_s {
    drmu_env_t * du;
    drmu_crtc_t * dc;
    unsigned int conn_n;
    unsigned int conn_size;
    drmu_conn_t ** dns;
    bool max_bpc_alloc;
};

int
drmu_atomic_add_output_props(drmu_atomic_t * const da, drmu_output_t * const dout)
{
#warning NIF
    return 0;
}

int
drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb)
{
#warning NIF
    return 0;
}


int
drmu_output_mode_id_set(drmu_output_t * const dout, const int mode_id)
{
#warning NIF
    return 0;
}

unsigned int
drmu_output_width(const drmu_output_t * const dout)
{
#warning NIF
    return 0;
}

unsigned int drmu_output_height(const drmu_output_t * const dout)
{
#warning NIF
    return 0;
}

int
drmu_mode_pick_simple_cb(void * v, const drmu_mode_pick_simple_params_t * mode)
{
    const drmu_mode_pick_simple_params_t * const p = v;

    const int pref = (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
    const unsigned int r_m = (uint32_t)(((uint64_t)mode->clock * 1000000) / (mode->htotal * mode->vtotal));
    const unsigned int r_f = p->hz_x_1000;

    // We don't understand interlace
    if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0)
        return -1;

    if (p->width == mode->hdisplay && p->height == mode->vdisplay)
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
drmu_output_mode_pick_simple(drmu_crtc_t * const dout, drmu_mode_score_fn * const score_fn, void * const score_v)
{
    int best_score = -1;
    int best_mode = -1;
    int i;

    for (i = 0;; ++i) {
        const drmu_mode_pick_simple_params_t sp = drmu_crtc_mode_simple_params(dout->dc, i);
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
            (dc_t = drmu_crtc_find_id(du, conn_id)) == NULL)
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

