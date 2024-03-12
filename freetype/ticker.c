#include "ticker.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"

#include <drm_fourcc.h>

enum ticker_state_e {
    TICKER_NEW = 0,
    TICKER_NEXT_CHAR,
    TICKER_SCROLL
};

struct ticker_env_s {
    enum ticker_state_e state;

    drmu_env_t *du;
    drmu_output_t *dout;
    drmu_plane_t *dp;
    drmu_fb_t *dfbs[2];

    drmu_rect_t pos;

    FT_Library    library;
    FT_Face       face;

    FT_Vector     pen;                    /* untransformed origin  */
    FT_Bool use_kerning;
    FT_UInt previous;

    unsigned int bn;
    int shl;

    int           target_height;
    int           n, num_chars;
    int crop_w;

    ticker_next_char_fn next_char_fn;
    void *next_char_v;
};

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static inline uint32_t
grey2argb(const uint32_t x)
{
    return ((x << 24) | (x << 16) | (x << 8) | (x));
}

void
draw_bitmap(drmu_fb_t *const dfb,
            FT_Bitmap *bitmap,
            FT_Int      x,
            FT_Int      y)
{
    int  i, j, p, q;
    const int fb_width = drmu_fb_width(dfb);
    const int fb_height = drmu_fb_height(dfb);
    const size_t fb_stride = drmu_fb_pitch(dfb, 0) / 4;
    uint32_t *const image = drmu_fb_data(dfb, 0);

    const int  x_max = MIN(fb_width, (int)(x + bitmap->width));
    const int  y_max = MIN(fb_height, (int)(y + bitmap->rows));

    /* for simplicity, we assume that `bitmap->pixel_mode' */
    /* is `FT_PIXEL_MODE_GRAY' (i.e., not a bitmap font)   */

    for (i = x, p = 0; i < x_max; i++, p++)
    {
        for (j = y, q = 0; j < y_max; j++, q++)
        {
            image[j * fb_stride + i] |= grey2argb(bitmap->buffer[q * bitmap->width + p]);
        }
    }
}

static void
shift_2d(void *dst, const void *src, size_t stride, size_t offset, size_t h)
{
    size_t i;
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (i = 0; i != h; ++i)
    {
        memcpy(d, s + offset, stride - offset);
        memset(d + stride - offset, 0, offset);
        d += stride;
        s += stride;
    }
}

void
ticker_next_char_cb_set(ticker_env_t *const te, const ticker_next_char_fn fn, void *const v)
{
    te->next_char_fn = fn;
    te->next_char_v = v;
}

static int
do_scroll(ticker_env_t *const te)
{
    if (te->shl > 0)
    {
        drmu_fb_t *const fb0 = te->dfbs[te->bn];
        int crop_w = 99; // **********

        drmu_atomic_t *da = drmu_atomic_new(te->du);
        drmu_fb_crop_frac_set(fb0, drmu_rect_shl16((drmu_rect_t) { .x = MAX(0, (int)drmu_fb_width(fb0) - crop_w - te->shl), .y = 0, .w = crop_w, .h = te->pos.h }));
        drmu_atomic_plane_add_fb(da, te->dp, fb0, te->pos);
        drmu_atomic_queue(&da);
        --te->shl;
    }

    if (te->shl <= 0)
        te->state = TICKER_NEXT_CHAR;

    return 0;
}

static int
do_render(ticker_env_t *const te)
{
    FT_Matrix matrix = {
        .xx = 0x10000L,
        .xy = 0,
        .yx = 0,
        .yy = 0x10000L
    };
    const FT_GlyphSlot slot = te->face->glyph;
    FT_UInt glyph_index;
    int c;
    drmu_fb_t *const fb1 = te->dfbs[te->bn];
    drmu_fb_t *const fb0 = te->dfbs[te->bn ^ 1];

    /* set transformation */
    FT_Set_Transform(te->face, &matrix, &te->pen);

    c = te->next_char_fn(te->next_char_v);
    if (c <= 0)
        return c;

    /* convert character code to glyph index */
    glyph_index = FT_Get_Char_Index(te->face, c);

    /* retrieve kerning distance and move pen position */
    if (te->use_kerning && te->previous && glyph_index)
    {
        FT_Vector delta = { 0, 0 };

        FT_Get_Kerning(te->face, te->previous, glyph_index, FT_KERNING_DEFAULT, &delta);

        te->pen.x += delta.x;
    }

    /* load glyph image into the slot (erase previous one) */
    if (FT_Load_Glyph(te->face, glyph_index, FT_LOAD_RENDER))
    {
        drmu_warn(te->du, "Load Glyph failed");
        return -1;
    }

    te->shl = (te->pen.x >> 6) + MAX(slot->bitmap.width, slot->advance.x >> 6) - te->pos.w;
    if (te->shl > 0)
    {
        te->pen.x -= te->shl << 6;
        shift_2d(drmu_fb_data(fb0, 0), drmu_fb_data(fb1, 0), drmu_fb_pitch(fb0, 0), te->shl * 4, drmu_fb_height(fb0));
    }

    /* now, draw to our target surface (convert position) */
    draw_bitmap(fb0, &slot->bitmap, te->pen.x >> 6, te->target_height - slot->bitmap_top);

    /* increment pen position */
    te->pen.x += slot->advance.x;

    printf("%ld, %ld, left=%d, top=%d\n", te->pen.x, te->pen.y, slot->bitmap_left, slot->bitmap_top);

    te->bn ^= 1;
    te->state = TICKER_SCROLL;
    return 0;
}

int
ticker_run(ticker_env_t *const te)
{
    int rv = -1;
    switch (te->state)
    {
        case TICKER_NEW:
        case TICKER_NEXT_CHAR:
            if ((rv = do_render(te)) < 0)
                break;
            /* FALLTHRU */
        case TICKER_SCROLL:
            rv = do_scroll(te);
            break;
        default:
            break;
    }
    return rv;
}

void
ticker_delete(ticker_env_t **ppTicker)
{
    ticker_env_t *const te = *ppTicker;
    if (te == NULL)
        return;

    if (te->dfbs[0])
    {
        drmu_atomic_t *da = drmu_atomic_new(te->du);
        drmu_atomic_plane_clear_add(da, te->dp);
        drmu_atomic_queue(&da);
    }

    drmu_fb_unref(te->dfbs + 0);
    drmu_fb_unref(te->dfbs + 1);
    drmu_plane_unref(&te->dp);
    drmu_output_unref(&te->dout);

    FT_Done_Face(te->face);
    FT_Done_FreeType(te->library);

    free(te);
}

int
ticker_set_face(ticker_env_t *const te, const char *const filename, const unsigned int size_req)
{
    unsigned int size = size_req == 0 ? te->pos.h / 2 : size_req;

// https://freetype.org/freetype2/docs/tutorial/step2.html

    if (FT_New_Face(te->library, filename, 0, &te->face))
    {
        drmu_err(te->du, "Face not found '%s'", filename);
        return -1;
    }

    if (FT_Set_Pixel_Sizes(te->face, 0, size))
    {
        drmu_err(te->du, "Bad char size\n");
        return -1;
    }

    te->pen.x = te->pos.w * 64;
    te->pen.y = (te->pos.h - size) * 32; // 64/2
    te->target_height = te->pos.h - -size / 2; // Top for rendering purposes

    te->use_kerning = FT_HAS_KERNING(te->face);
    return 0;
}

ticker_env_t*
ticker_new(drmu_output_t *dout, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    const uint32_t format = DRM_FORMAT_ARGB8888;
    const uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    ticker_env_t *te = calloc(1, sizeof(*te));

    if (te == NULL)
        return NULL;

    te->dout = drmu_output_ref(dout);
    te->du = drmu_output_env(dout);

    te->pos = (drmu_rect_t) { x, y, w, h };

    if (FT_Init_FreeType(&te->library) != 0)
    {
        drmu_err(te->du, "Failed to init FreeType");
        goto fail;
    }

    // This doesn't really want to be the primary
    if ((te->dp = drmu_output_plane_ref_format(te->dout, DRMU_PLANE_TYPE_OVERLAY, format, modifier)) == NULL)
    {
        drmu_err(te->du, "Failed to find output plane");
        goto fail;
    }

    for (unsigned int i = 0; i != 2; ++i)
    {
        if ((te->dfbs[i] = drmu_fb_new_dumb_mod(te->du, w, h, format, modifier)) == NULL)
        {
            drmu_err(te->du, "Failed to get frame buffer");
            goto fail;
        }
    }

    memset(drmu_fb_data(te->dfbs[0], 0), 0x80, w * h * 4); // ????

    return te;

fail:
    ticker_delete(&te);
    return NULL;
}

