#include "fx.h"
#include <string.h>
#include <math.h>

FxParams g_fx;

void fx_defaults(FxParams *p)
{
    memset(p, 0, sizeof *p);
    p->rev_size = 72; p->rev_damp = 50; p->rev_level = 48;
    p->cho_rate = 30; p->cho_depth = 40; p->cho_level = 0;
    p->eq_lo = 64; p->eq_mid = 64; p->eq_hi = 64;
    p->limiter = 1;
}

static int s_rate = 48000;

/* ---- reverb: Freeverb, eight combs and four allpasses per side ---- */
#define NCOMB 8
#define NAP 4
static const int COMB_44[NCOMB] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int AP_44[NAP] = { 556, 441, 341, 225 };
#define SPREAD 23
typedef struct { float *buf; int len, idx; float store; } Comb;
typedef struct { float *buf; int len, idx; } Allpass;
static float s_comb_mem[2][NCOMB][2048];
static float s_ap_mem[2][NAP][1024];
static Comb s_comb[2][NCOMB];
static Allpass s_ap[2][NAP];
static float s_rev_fb = 0.84f, s_rev_damp = 0.4f, s_rev_wet = 0.3f;
static int s_rev_size_c = -1, s_rev_damp_c = -1, s_rev_level_c = -1;

static inline float comb_run(Comb *c, float in, float fb, float damp)
{
    float out = c->buf[c->idx];
    c->store = out * (1.0f - damp) + c->store * damp;
    c->buf[c->idx] = in + c->store * fb;
    if (++c->idx >= c->len) c->idx = 0;
    return out;
}
static inline float ap_run(Allpass *a, float in)
{
    float bufout = a->buf[a->idx];
    float out = -in + bufout;
    a->buf[a->idx] = in + bufout * 0.5f;
    if (++a->idx >= a->len) a->idx = 0;
    return out;
}

/* ---- chorus: one modulated delay line, two taps 90 degrees apart ---- */
#define CHO_LEN 4096
static float s_cho_buf[2][CHO_LEN];
static int s_cho_w = 0;
static float s_cho_phase = 0.0f, s_cho_inc = 0.0f, s_cho_depth = 0.0f, s_cho_wet = 0.0f;
static int s_cho_rate_c = -1, s_cho_depth_c = -1, s_cho_level_c = -1;

/* ---- EQ: low shelf, peak, high shelf, per side ---- */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;
static Biquad s_eq[2][3];
static int s_eq_c[3] = { -1, -1, -1 };

static void biquad_shelf(Biquad *q, float f0, float gain_db, int high)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / s_rate, cw = cosf(w0), sw = sinf(w0);
    float S = 1.0f, alpha = sw / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    float sa = 2.0f * sqrtf(A) * alpha;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa;
    } else {
        b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static void biquad_peak(Biquad *q, float f0, float gain_db, float Q)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / s_rate, cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A;
    float a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static inline float biquad_run(Biquad *q, float in)
{
    float out = q->b0 * in + q->z1;
    q->z1 = q->b1 * in - q->a1 * out + q->z2;
    q->z2 = q->b2 * in - q->a2 * out;
    return out;
}

/* ---- limiter: peak follower, instant attack, slow release ---- */
static float s_lim_env = 0.0f;
#define LIM_THRESH 0.89f      /* -1 dB */
#define LIM_RELEASE 0.9995f   /* about 40 ms per 3 dB at 48 kHz */

void fx_init(int rate)
{
    s_rate = rate;
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < NCOMB; i++) {
            int len = COMB_44[i] * rate / 44100 + (s ? SPREAD : 0);
            if (len > 2048) len = 2048;
            s_comb[s][i].buf = s_comb_mem[s][i]; s_comb[s][i].len = len; s_comb[s][i].idx = 0; s_comb[s][i].store = 0;
        }
        for (int i = 0; i < NAP; i++) {
            int len = AP_44[i] * rate / 44100 + (s ? SPREAD : 0);
            if (len > 1024) len = 1024;
            s_ap[s][i].buf = s_ap_mem[s][i]; s_ap[s][i].len = len; s_ap[s][i].idx = 0;
        }
    }
    memset(s_comb_mem, 0, sizeof s_comb_mem);
    memset(s_ap_mem, 0, sizeof s_ap_mem);
    memset(s_cho_buf, 0, sizeof s_cho_buf);
    memset(s_eq, 0, sizeof s_eq);
    s_rev_size_c = s_rev_damp_c = s_rev_level_c = -1;
    s_cho_rate_c = s_cho_depth_c = s_cho_level_c = -1;
    s_eq_c[0] = s_eq_c[1] = s_eq_c[2] = -1;
}

static void retune(void)
{
    if (g_fx.rev_size != s_rev_size_c) { s_rev_size_c = g_fx.rev_size; s_rev_fb = 0.70f + 0.28f * (s_rev_size_c / 127.0f); }
    if (g_fx.rev_damp != s_rev_damp_c) { s_rev_damp_c = g_fx.rev_damp; s_rev_damp = 0.05f + 0.7f * (s_rev_damp_c / 127.0f); }
    if (g_fx.rev_level != s_rev_level_c) { s_rev_level_c = g_fx.rev_level; float l = s_rev_level_c / 127.0f; s_rev_wet = l * l * 0.9f; }
    if (g_fx.cho_rate != s_cho_rate_c) { s_cho_rate_c = g_fx.cho_rate; float hz = 0.1f + 4.9f * (s_cho_rate_c / 127.0f); s_cho_inc = hz / s_rate; }
    if (g_fx.cho_depth != s_cho_depth_c) { s_cho_depth_c = g_fx.cho_depth; s_cho_depth = (1.0f + 7.0f * (s_cho_depth_c / 127.0f)) * 0.001f * s_rate; }
    if (g_fx.cho_level != s_cho_level_c) { s_cho_level_c = g_fx.cho_level; s_cho_wet = s_cho_level_c / 127.0f; }
    if (g_fx.eq_lo != s_eq_c[0]) { s_eq_c[0] = g_fx.eq_lo; float g = (s_eq_c[0] - 64) * (12.0f / 63.0f); biquad_shelf(&s_eq[0][0], 150.0f, g, 0); s_eq[1][0] = s_eq[0][0]; }
    if (g_fx.eq_mid != s_eq_c[1]) { s_eq_c[1] = g_fx.eq_mid; float g = (s_eq_c[1] - 64) * (12.0f / 63.0f); biquad_peak(&s_eq[0][1], 1200.0f, g, 0.8f); s_eq[1][1] = s_eq[0][1]; }
    if (g_fx.eq_hi != s_eq_c[2]) { s_eq_c[2] = g_fx.eq_hi; float g = (s_eq_c[2] - 64) * (12.0f / 63.0f); biquad_shelf(&s_eq[0][2], 6000.0f, g, 1); s_eq[1][2] = s_eq[0][2]; }
}

void fx_process(float *dry, const float *rev, const float *cho, int frames)
{
    retune();
    int eq_on = g_fx.eq_lo != 64 || g_fx.eq_mid != 64 || g_fx.eq_hi != 64;
    for (int i = 0; i < frames; i++) {
        float l = dry[2 * i], r = dry[2 * i + 1];
        /* chorus: delayed and swept send bus, both sides from the mono sum */
        if (s_cho_wet > 0.0f) {
            float in = (cho[2 * i] + cho[2 * i + 1]) * 0.5f;
            s_cho_buf[0][s_cho_w] = in;
            float ph = s_cho_phase * 2.0f * (float)M_PI;
            float base = 0.020f * s_rate;
            float d0 = base + s_cho_depth * (0.5f + 0.5f * sinf(ph));
            float d1 = base + s_cho_depth * (0.5f + 0.5f * cosf(ph));
            for (int t = 0; t < 2; t++) {
                float d = t ? d1 : d0;
                float pos = (float)s_cho_w - d;
                while (pos < 0) pos += CHO_LEN;
                int i0 = (int)pos, i1 = (i0 + 1) & (CHO_LEN - 1);
                float fr = pos - i0;
                float v = s_cho_buf[0][i0 & (CHO_LEN - 1)] * (1.0f - fr) + s_cho_buf[0][i1] * fr;
                if (t) r += v * s_cho_wet; else l += v * s_cho_wet;
            }
            s_cho_w = (s_cho_w + 1) & (CHO_LEN - 1);
            s_cho_phase += s_cho_inc;
            if (s_cho_phase >= 1.0f) s_cho_phase -= 1.0f;
        }
        /* reverb: send bus through the combs in parallel, then the allpasses in series */
        if (s_rev_wet > 0.0f) {
            float in = (rev[2 * i] + rev[2 * i + 1]) * 0.015f;
            float outl = 0, outr = 0;
            for (int c = 0; c < NCOMB; c++) {
                outl += comb_run(&s_comb[0][c], in, s_rev_fb, s_rev_damp);
                outr += comb_run(&s_comb[1][c], in, s_rev_fb, s_rev_damp);
            }
            for (int a = 0; a < NAP; a++) {
                outl = ap_run(&s_ap[0][a], outl);
                outr = ap_run(&s_ap[1][a], outr);
            }
            l += outl * s_rev_wet;
            r += outr * s_rev_wet;
        }
        if (eq_on) {
            for (int k = 0; k < 3; k++) { l = biquad_run(&s_eq[0][k], l); r = biquad_run(&s_eq[1][k], r); }
        }
        if (g_fx.limiter) {
            float peak = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
            if (peak > s_lim_env) s_lim_env = peak; else s_lim_env *= LIM_RELEASE;
            if (s_lim_env > LIM_THRESH) { float g = LIM_THRESH / s_lim_env; l *= g; r *= g; }
        }
        if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
        dry[2 * i] = l; dry[2 * i + 1] = r;
    }
}
