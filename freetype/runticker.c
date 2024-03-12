#include "runticker.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <drmu_output.h>

typedef struct ticker_env_s {
    struct drmu_output_s * dout;
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
} ticker_env_t;

static void *
cube_thread(void * v)
{
    ticker_env_t * const te = v;

    drmu_output_unref(&te->dout);
    free(te);
    return NULL;
}

int
runticker_start(struct drmu_output_s * dout, unsigned int x, unsigned int t, unsigned int w, unsigned int h)
{
    pthread_t thread_id;
    ticker_env_t * te = calloc(1, sizeof(*te));

    if (te == NULL)
        return -1;

    te->dout = drmu_output_ref(dout);
    te->x = x;
    te->y = y;
    te->w = w;
    te->h = h;


    pthread_create(&thread_id, NULL, ticker_thread, te);
    return 0;
}

