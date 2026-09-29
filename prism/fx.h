/* Effects rack: reverb and chorus fed by per-part sends, then a three-band EQ
 * and a limiter on the master. Float, stereo interleaved, at the output rate.
 * The panel writes the parameters; the audio thread reads them every block. */
#ifndef FX_H
#define FX_H
#include <stdint.h>

typedef struct {
    uint8_t rev_size, rev_damp, rev_level;   /* 0..127 */
    uint8_t cho_rate, cho_depth, cho_level;
    uint8_t eq_lo, eq_mid, eq_hi;            /* 64 = flat, +-12 dB */
    uint8_t limiter;                         /* 0 off, 1 on */
    uint8_t pad[2];
} FxParams;

extern FxParams g_fx;

void fx_defaults(FxParams *p);
void fx_init(int rate);
/* dry: the part mix, processed in place; rev and cho: the send buses (may be silent) */
void fx_process(float *dry, const float *rev, const float *cho, int frames);

#endif
