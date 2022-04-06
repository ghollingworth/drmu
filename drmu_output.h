#include "drmu.h"

// Add all props accumulated on the output to the atomic
int drmu_atomic_add_output_props(drmu_atomic_t * const da, drmu_output_t * const dout);

// Set FB info (bit-depth, HDR metadata etc.)
int drmu_output_fb_info_set(drmu_output_t * const dout, const drmu_fb_t * const fb);

// Set output mode
int drmu_output_mode_id_set(drmu_output_t * const dout, const int mode_id);

// Width/height of the currebnt mode
unsigned int drmu_output_width(const drmu_output_t * const dout);
unsigned int drmu_output_height(const drmu_output_t * const dout);

typedef int drmu_mode_score_fn drmu_mode_score(void * v, const drmu_mode_pick_simple_params_t * mode);

int drmu_output_mode_pick_simple(drmu_crtc_t * const dc, drmu_mode_score_fn * const score_fn, void * const score_v);

// Simple mode picker cb - looks for width / height and then refresh
// If nothing "plausible" defaults to EDID preferred mode
drmu_mode_score_fn drmu_mode_pick_simple_cb;

// Allow fb max_bpc info to set the output mode (default false)
int drmu_output_max_bpc_allow(drmu_output_t * const dout, const bool allow);

// Add a CONN/CRTC pair to an output
// If conn_name == NULL then 1st connected connector is used
// If != NULL then 1st conn with prefix-matching name is used
int drmu_output_add_output(drmu_output_t * const dout, const char * const conn_name);

// Create a new empty output - has no crtc or conn
drmu_output_t * drmu_output_new(drmu_env_t * const du);

// Unref an output
void drmu_output_unref(drmu_output_t ** const ppdout);


