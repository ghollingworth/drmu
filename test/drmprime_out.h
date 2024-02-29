#include <libavutil/rational.h>

struct AVFrame;
struct AVCodecContext;
typedef struct drmprime_out_env_s drmprime_out_env_t;

int drmprime_out_get_buffer2(struct AVCodecContext *s, struct AVFrame *frame, int flags);
int drmprime_out_display(drmprime_out_env_t * dpo, struct AVFrame * frame);
int drmprime_out_modeset(drmprime_out_env_t *dpo, int w, int h, const AVRational rate);
void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new();

