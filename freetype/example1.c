/* Simple example program that uses ticker to scroll a message across the
 * screen
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>

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
  int prod_fd;
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

static void
do_prod(void * v)
{
  static const uint64_t one = 1;
  dft_env_t * const dfte = v;
  write(dfte->prod_fd, &one, sizeof(one));
}

int
main( int     argc,
      char**  argv )
{
  dft_env_t * dfte = calloc(1, sizeof(*dfte));
  drmu_env_t * du;
  drmu_output_t * dout;
  int trv;

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
  drmu_output_unref(&dout); // Ticker keeps a ref

  if (ticker_set_face(dfte->te, filename) != 0)
  {
    fprintf(stderr, "Failed to set face\n");
    return 1;
  }

  ticker_next_char_cb_set(dfte->te, next_char_cb, dfte);

  if ((dfte->prod_fd = eventfd(0, EFD_NONBLOCK)) == -1)
  {
    fprintf(stderr, "Failed to get event fd\n");
    return 1;
  }
  ticker_commit_cb_set(dfte->te, do_prod, dfte);

  if (ticker_init(dfte->te) != 0)
  {
    fprintf(stderr, "Failed to init ticker\n");
    return 1;
  }

  while ((trv = ticker_run(dfte->te)) >= 0)
  {
    char evt_buf[8];
    int rv;

    struct pollfd pf[2] = {
      {.fd = dfte->prod_fd, .events = POLLIN, .revents = 0},
      {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
    };

    while ((rv = poll(pf, 2, -1)) < 0 && errno == EINTR)
      /* loop */;

    /* 0 = timeout but that should never happen */
    if (rv <= 0) {
      fprintf(stderr, "Poll failure\n");
      break;
    }

    /* Keypress */
    if (pf[1].revents != 0)
      break;

    read(dfte->prod_fd, evt_buf, 8);
  }

  if (trv < 0)
    getchar();

  ticker_delete(&dfte->te);
  close(dfte->prod_fd);
  free(dfte);
  return 0;
}

/* EOF */
