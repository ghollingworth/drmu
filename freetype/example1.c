/* example1.c                                                      */
/*                                                                 */
/* This small program shows how to print a rotated string with the */
/* FreeType 2 library.                                             */


#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_scan.h"

#include <drm_fourcc.h>

#define WIDTH   640
#define HEIGHT  128

typedef struct dft_env_s {
  drmu_env_t * du;
  drmu_output_t * dout;
  drmu_plane_t * dp;
  drmu_fb_t * dfbs[2];
} dft_env_t;

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

/* Replace this function with something useful. */

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
draw_bitmap( drmu_fb_t * const dfb,
    FT_Bitmap*  bitmap,
             FT_Int      x,
             FT_Int      y)
{
  int  i, j, p, q;
  const int fb_width = drmu_fb_width(dfb);
  const int fb_height = drmu_fb_height(dfb);
  const size_t fb_stride = drmu_fb_pitch(dfb, 0) / 4;
  uint32_t * const image = drmu_fb_data(dfb, 0);

  const int  x_max = MIN(fb_width, (int)(x + bitmap->width));
  const int  y_max = MIN(fb_height, (int)(y + bitmap->rows));

  /* for simplicity, we assume that `bitmap->pixel_mode' */
  /* is `FT_PIXEL_MODE_GRAY' (i.e., not a bitmap font)   */

  for ( i = x, p = 0; i < x_max; i++, p++ )
  {
    for ( j = y, q = 0; j < y_max; j++, q++ )
    {
      image[j * fb_stride + i] |= grey2argb(bitmap->buffer[q * bitmap->width + p]);
    }
  }
}

static void
shift_2d(void * dst, const void * src, size_t stride, size_t offset, size_t h)
{
  size_t i;
  uint8_t * d = dst;
  const uint8_t * s = src;

  for (i = 0; i != h; ++i)
  {
    memcpy(d, s + offset, stride - offset);
    memset(d + stride - offset, 0, offset);
    d += stride;
    s += stride;
  }
}

int
main( int     argc,
      char**  argv )
{
  FT_Library    library;
  FT_Face       face;

  FT_GlyphSlot  slot;
  FT_Matrix     matrix;                 /* transformation matrix */
  FT_Vector     pen;                    /* untransformed origin  */
  FT_Error      error;
  FT_Bool use_kerning;
  FT_UInt previous = 0;

  char*         filename;
  char*         text;

//  double        angle;
  int           target_height;
  int           n, num_chars;
  int crop_w = WIDTH - 96;

  dft_env_t * dfte = calloc(1, sizeof(*dfte));

  const uint32_t format = DRM_FORMAT_ARGB8888;
  const uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
  const char * const device = NULL;
  unsigned int i;

  static const drmu_log_env_t log = {
      .fn = drmu_log_stderr_cb,
      .v = NULL,
      .max_level = DRMU_LOG_LEVEL_INFO
  };

  if ( argc != 3 )
  {
    fprintf ( stderr, "usage: %s font sample-text\n", argv[0] );
    exit( 1 );
  }

  if ( dfte == NULL )
  {
    fprintf(stderr, "No memory for env\n");
    return 1;
  }

  filename      = argv[1];                           /* first argument     */
  text          = argv[2];                           /* second argument    */
  num_chars     = strlen( text );
//  angle         = ( 25.0 / 360 ) * 3.14159 * 2;      /* use 25 degrees     */
  target_height = HEIGHT;

  error = FT_Init_FreeType( &library );              /* initialize library */
  if ( error )
  {
    fprintf(stderr, "Library init failed\n");
    return 1;
  }

  error = FT_New_Face( library, filename, 0, &face );/* create face object */
  error = FT_Init_FreeType( &library );              /* initialize library */
  if ( error )
  {
    fprintf(stderr, "Face not found '%s'\n", filename);
    return 1;
  }

  /* use 50pt at 100dpi */
//  error = FT_Set_Char_Size( face, 50 * 64, 0, 100, 0 );                /* set character size */
  error = FT_Set_Pixel_Sizes(face, 0, 64);
  if (error)
  {
    fprintf(stderr, "Bad char size\n");
    return 1;
  }

  if (drmu_scan_output(device, &log, &dfte->du, &dfte->dout) != 0) {
      fprintf(stderr, "Failed drmu scan for device\n");
      return 1;
  }
  drmu_env_restore_enable(dfte->du);

  // This doesn't really want to be the primary
  if ((dfte->dp = drmu_output_plane_ref_format(dfte->dout, DRMU_PLANE_TYPE_OVERLAY, format, modifier)) == NULL)
  {
      fprintf(stderr, "Failed to find output plane\n");
      return 1;
  }

  for (i = 0; i != 2; ++i)
  {
    if ( (dfte->dfbs[i] = drmu_fb_new_dumb_mod(dfte->du, WIDTH, HEIGHT, format, modifier)) == NULL )
    {
      fprintf(stderr, "Failed to get frame buffer\n");
      return 1;
    }
  }


  /* cmap selection omitted;                                        */
  /* for simplicity we assume that the font contains a Unicode cmap */

  slot = face->glyph;

  /* set up matrix */
  matrix.xx = 0x10000L;
  matrix.xy = 0;
  matrix.yx = 0;
  matrix.yy = 0x10000L;

  /* the pen position in 26.6 cartesian space coordinates; */
  /* start at (300,200) relative to the upper left corner  */
  pen.x = WIDTH * 64;
  pen.y =  32 * 64;

  use_kerning = FT_HAS_KERNING( face );

  memset(drmu_fb_data(dfte->dfbs[0], 0), 0x80, WIDTH * HEIGHT * 4);

  i = 0;
  for ( n = 0; n < num_chars; n++, i ^= 1 )
  {
    int shl;
    int j;

    /* set transformation */
    FT_Set_Transform( face, &matrix, &pen );

#if 0
    /* load glyph image into the slot (erase previous one) */
    error = FT_Load_Char( face, text[n], FT_LOAD_RENDER );
    if ( error )
      continue;                 /* ignore errors */
#else
  /* convert character code to glyph index */
  FT_UInt glyph_index = FT_Get_Char_Index( face, text[n] );

  /* retrieve kerning distance and move pen position */
  if ( use_kerning && previous && glyph_index )
  {
    FT_Vector  delta;

    FT_Get_Kerning( face, previous, glyph_index,
                    FT_KERNING_DEFAULT, &delta );

    pen.x += delta.x;
  }

  /* load glyph image into the slot (erase previous one) */
  error = FT_Load_Glyph( face, glyph_index, FT_LOAD_RENDER );
  if ( error )
    continue;  /* ignore errors */
#endif

    shl = (pen.x >> 6)+ MAX(slot->bitmap.width, slot->advance.x >> 6) - WIDTH;
    if ( shl > 0 )
    {
      pen.x -= shl << 6;
      shift_2d(drmu_fb_data(dfte->dfbs[i], 0), drmu_fb_data(dfte->dfbs[i ^ 1], 0), drmu_fb_pitch(dfte->dfbs[i], 0), shl * 4, drmu_fb_height(dfte->dfbs[i]));
    }

    /* now, draw to our target surface (convert position) */
    draw_bitmap(dfte->dfbs[i],
         &slot->bitmap, pen.x >> 6,
                 target_height - slot->bitmap_top );

    /* increment pen position */
    pen.x += slot->advance.x;

    printf("%ld, %ld, left=%d, top=%d\n", pen.x, pen.y, slot->bitmap_left, slot->bitmap_top);

    for (j = 0; j < shl; ++j)
    {
      drmu_atomic_t * da = drmu_atomic_new(dfte->du);

      drmu_fb_crop_frac_set(dfte->dfbs[i], drmu_rect_shl16((drmu_rect_t) {.x = MAX(0, WIDTH - crop_w - shl + j), .y = 0, .w = crop_w, .h = HEIGHT }));

      drmu_atomic_plane_add_fb(da, dfte->dp, dfte->dfbs[i], drmu_rect_wh(crop_w, HEIGHT));
      drmu_atomic_queue(&da);
      usleep(30000);
    }

  }


  getchar();

  FT_Done_Face    ( face );
  FT_Done_FreeType( library );

  return 0;
}

/* EOF */
