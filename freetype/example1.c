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

#include "ticker.h"

#define WIDTH   640
#define HEIGHT  128

typedef struct dft_env_s {
  ticker_env_t * te;
  const char * text;
  const char * cchar;
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

static int
next_char_cb(void * v)
{
  dft_env_t * const dfte = v;

  if (*dfte->cchar == 0)
    return -1;
  return *dfte->cchar++;
}

int
main( int     argc,
      char**  argv )
{
  dft_env_t * dfte = calloc(1, sizeof(*dfte));
  drmu_env_t * du;
  drmu_output_t * dout;

  const char * const device = NULL;
  const char * filename;

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
  dfte->text          = argv[2];                           /* second argument    */
  dfte->cchar = dfte->text;

  if (drmu_scan_output(device, &log, &du, &dout) != 0) {
      fprintf(stderr, "Failed drmu scan for device\n");
      return 1;
  }
  drmu_env_restore_enable(du);

  if ((dfte->te = ticker_new(dout, 0, 128, 720, 128)) == NULL)
  {
    fprintf(stderr, "Failed to create ticker\n");
    return 1;
  }

  if (ticker_set_face(dfte->te, filename, 0) != 0)
  {
    fprintf(stderr, "Failed to set face\n");
    return 1;
  }

  ticker_next_char_cb_set(dfte->te, next_char_cb, dfte);

  while (ticker_run(dfte->te) >= 0)
  {
    usleep(30000);
  }

  getchar();

  ticker_delete(&dfte->te);
  free(dfte);
  return 0;
}

/* EOF */
