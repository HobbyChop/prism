/* SoundFont engine and MIDI input.
 * TinySoundFont renders sixteen GM channels at 48 kHz stereo. The audio thread
 * owns the engine: it drains panel events, reads adapter packets, maintains the
 * part table and renders 256-frame blocks into the platform ring. Bank loads
 * happen on the main thread into a second instance that is swapped in between
 * blocks. */
/* sample data goes into a dedicated memory block (see plat.h) */
#include <stdlib.h>
#include "plat.h"
#define TSF_MALLOC  plat_bigalloc
#define TSF_FREE    plat_bigfree
#define TSF_REALLOC plat_bigrealloc
#define TSF_IMPLEMENTATION
#include "tsf/tsf.h"
#include "synth.h"
#include "plat.h"
#include "fx.h"
#include "mt32.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

SynPart g_parts[SYN_PARTS];
volatile int g_syn_voices = 0;
volatile int g_syn_poly = 48;
volatile int g_syn_master = 100;
volatile int g_syn_load = 0;
volatile int g_syn_load_avg = 0;
volatile int g_syn_mode = 0;
char g_syn_bank_name[64];
long g_syn_bank_bytes = 0;

static tsf *s_f = NULL;                 /* engine used by the audio thread */
static tsf *volatile s_pending = NULL;  /* loaded bank waiting to be swapped in */
static tsf *volatile s_retired = NULL;  /* previous bank, freed by the main thread */
static volatile int s_unload_req = 0;   /* unload request from the main thread */
static volatile int s_ready = 0;

/* ---- event ring (panel to audio) ---- */
static volatile uint32_t s_evq[256];
static volatile unsigned int s_ev_w = 0, s_ev_r = 0;

void syn_event(int kind, int part, int a, int b)
{
    unsigned int next = (s_ev_w + 1) & 255;
    if (next == s_ev_r) return;
    s_evq[s_ev_w] = ((uint32_t)kind << 24) | ((uint32_t)(part & 0xFF) << 16) | ((uint32_t)(a & 0xFF) << 8) | (uint32_t)(b & 0xFF);
    s_ev_w = next;
}

/* ---- parts ---- */
static void part_apply_tone(int ch)
{
    SynPart *p = &g_parts[ch];
    float fc = ((int)p->cutoff - 64) * 90.0f;          /* +-5760 cents, four and a half octaves */
    float q = p->reso * (18.0f / 127.0f);
    float atk = powf(2.0f, ((int)p->attack - 64) / 24.0f);
    float rel = powf(2.0f, ((int)p->release - 64) / 24.0f);
    if (s_f) tsf_channel_set_tone(s_f, ch, fc, q, atk, rel);
}

static void part_apply_program(int ch)
{
    if (!s_f) return;
    SynPart *p = &g_parts[ch];
    int bank = (ch == 9 && p->bank == 0) ? 128 : p->bank;
    if (!tsf_channel_set_bank_preset(s_f, ch, bank, p->prog) &&
        !tsf_channel_set_bank_preset(s_f, ch, 0, p->prog))
        tsf_channel_set_presetnumber(s_f, ch, p->prog, ch == 9);
    int idx = tsf_channel_get_preset_index(s_f, ch);
    const char *n = (idx >= 0 && idx < tsf_get_presetcount(s_f)) ? tsf_get_presetname(s_f, idx) : "";
    snprintf(p->name, sizeof(p->name), "%.23s", n ? n : "");
}

static void part_apply_mix(int ch)
{
    if (!s_f) return;
    SynPart *p = &g_parts[ch];
    float v = p->mute ? 0.0f : powf(p->level / 127.0f, 2.0f);
    tsf_channel_set_volume(s_f, ch, v);
    tsf_channel_set_pan(s_f, ch, p->pan / 127.0f);
}

static void parts_reset(void)
{
    for (int ch = 0; ch < SYN_PARTS; ch++) {
        SynPart *p = &g_parts[ch];
        p->level = 100; p->pan = 64; p->cutoff = 64; p->reso = 0; p->attack = 64; p->release = 64;
        p->mute = 0; p->notes = 0; p->meter = 0; p->bank = 0;
        p->rx = (uint8_t)ch; p->low = 0; p->high = 127; p->trans = 0;
        p->rev = 40; p->cho = 0;
        /* default layout: keyboard on 1, bass on 2, drums on 10 */
        p->prog = (ch == 0) ? 0 : (ch == 1) ? 33 : (ch == 2) ? 88 : (ch == 3) ? 48 : 0;
        if (ch == 9) p->prog = 0;
    }
}

static void parts_bind(void)   /* rebind every part after a bank swap */
{
    if (!s_f) return;
    tsf_set_output(s_f, TSF_STEREO_INTERLEAVED, OUT_RATE, -6.0f + (g_syn_master - 100) * 0.12f);
    tsf_set_max_voices(s_f, g_syn_poly);
    for (int ch = 0; ch < SYN_PARTS; ch++) {
        part_apply_program(ch);
        part_apply_mix(ch);
        part_apply_tone(ch);
    }
}

/* ---- loading (main thread) ---- */
/* The bank is read through a custom stream so the loading screen can report
 * progress. The library requests the whole sample chunk in one read; it is
 * served a megabyte at a time. */
static SynProgress s_progress = NULL;
void syn_set_progress(SynProgress cb) { s_progress = cb; }

struct load_stream { FILE *f; long total, done; int pct; };

static int ls_read(void *data, void *ptr, unsigned int size)
{
    struct load_stream *ls = (struct load_stream *)data;
    unsigned char *p = (unsigned char *)ptr;
    unsigned int left = size, got = 0;
    while (left) {
        unsigned int n = left > (1u << 20) ? (1u << 20) : left;
        size_t r = fread(p, 1, n, ls->f);
        if (r == 0) break;
        p += r; left -= (unsigned int)r; got += (unsigned int)r; ls->done += (long)r;
        if (s_progress && ls->total > 0) {
            int pct = (int)((unsigned long long)ls->done * 100ull / (unsigned long long)ls->total);   /* 64-bit: 32-bit would wrap past 21 MB */
            if (pct != ls->pct) { ls->pct = pct; s_progress(ls->done, ls->total); }
        }
    }
    return (int)got;
}

static int ls_skip(void *data, unsigned int count)
{
    struct load_stream *ls = (struct load_stream *)data;
    ls->done += (long)count;
    return fseek(ls->f, (long)count, SEEK_CUR) == 0;
}

int syn_load(const char *path)
{
    struct load_stream ls;
    ls.f = fopen(path, "rb");
    if (!ls.f) return -1;
    ls.total = plat_file_size(path);
    ls.done = 0;
    ls.pct = -1;
    struct tsf_stream stream;
    stream.data = &ls;
    stream.read = ls_read;
    stream.skip = ls_skip;
    if (s_progress) s_progress(0, ls.total);
    tsf *nf = tsf_load(&stream);
    fclose(ls.f);
    if (!nf) return -1;
    if (s_progress) s_progress(ls.total, ls.total);
    const char *base = strrchr(path, '/');
    snprintf(g_syn_bank_name, sizeof(g_syn_bank_name), "%.63s", base ? base + 1 : path);
    /* hand over; the audio thread swaps at its next block */
    while (s_pending) plat_sleep_ms(5);
    s_pending = nf;
    while (s_pending) plat_sleep_ms(5);
    if (s_retired) { tsf_close(s_retired); s_retired = NULL; }
    g_syn_bank_bytes = plat_file_size(path);
    if (g_syn_bank_bytes < 0) g_syn_bank_bytes = 0;
    return 0;
}

void syn_unload(void)
{
    while (s_pending) plat_sleep_ms(5);
    s_unload_req = 1;
    while (s_unload_req) plat_sleep_ms(5);
    if (s_retired) { tsf_close(s_retired); s_retired = NULL; }
    g_syn_bank_name[0] = 0;
    g_syn_bank_bytes = 0;
}

/* performance staged by the main thread, taken by the audio thread */
static SynPart s_perf_parts[SYN_PARTS];
static volatile int s_perf_pending = 0, s_perf_master = 100, s_perf_poly = 48;
void syn_perf_set(const SynPart *parts, int master, int poly)
{
    while (s_perf_pending) plat_sleep_ms(2);
    memcpy(s_perf_parts, parts, sizeof s_perf_parts);
    s_perf_master = master; s_perf_poly = poly;
    s_perf_pending = 1;
    while (s_perf_pending) plat_sleep_ms(2);
}
static void perf_take(void)
{
    if (s_f) for (int c = 0; c < SYN_PARTS; c++) tsf_channel_sounds_off_all(s_f, c);
    for (int i = 0; i < SYN_PARTS; i++) {
        SynPart *p = &g_parts[i];
        const SynPart *q = &s_perf_parts[i];
        p->prog = q->prog; p->bank = q->bank; p->level = q->level; p->pan = q->pan;
        p->cutoff = q->cutoff; p->reso = q->reso; p->attack = q->attack; p->release = q->release;
        p->mute = q->mute; p->rx = q->rx; p->low = q->low; p->high = q->high; p->trans = q->trans;
        p->rev = q->rev; p->cho = q->cho;
        p->notes = 0; p->meter = 0;
    }
    g_syn_master = s_perf_master;
    g_syn_poly = s_perf_poly < 8 ? 8 : s_perf_poly;
    parts_bind();
    s_perf_pending = 0;
}

int syn_ready(void) { return s_ready; }
int syn_preset_count(void) { return s_f ? tsf_get_presetcount(s_f) : 0; }
int syn_preset_info(int idx, int *bank, int *num, const char **name)
{
    if (!s_f || idx < 0 || idx >= s_f->presetNum) return 0;
    if (bank) *bank = s_f->presets[idx].bank;
    if (num) *num = s_f->presets[idx].preset;
    if (name) *name = s_f->presets[idx].presetName;
    return 1;
}
const char *syn_preset_name_of(int part) { return g_parts[part & 15].name; }

/* ---- MIDI in ---- */
/* A channel message goes to every part listening on that channel (its own
 * channel, omni, or off). Notes are filtered by key range and transposed, which
 * is how one keyboard channel becomes a split or a layer. The engine channel is
 * the part index. */
static void midi_packet(const unsigned char *pkt)
{
    int cin = pkt[0] & 0x0F, ch = pkt[1] & 0x0F, d1 = pkt[2], d2 = pkt[3];
    if (cin < 0x8 || cin == 0xF) return;
    for (int i = 0; i < SYN_PARTS; i++) {
        SynPart *p = &g_parts[i];
        if (!(p->rx == ch || p->rx == SYN_RX_OMNI)) continue;
        switch (cin) {
        case 0x9:
            if (d2 > 0) {
                if (d1 < p->low || d1 > p->high) break;
                int n = d1 + p->trans;
                if (n < 0 || n > 127) break;
                if (!p->mute && s_f) tsf_channel_note_on(s_f, i, n, d2 / 127.0f);
                if (p->notes < 255) p->notes++;
                float m = d2 / 127.0f;
                if (m > p->meter) p->meter = m;
                break;
            }
            /* fall through: velocity 0 is a note off */
        case 0x8: {
            if (d1 < p->low || d1 > p->high) break;
            int n = d1 + p->trans;
            if (n < 0 || n > 127) break;
            if (s_f) tsf_channel_note_off(s_f, i, n);
            if (p->notes) p->notes--;
            break;
        }
        case 0xB:
            if (!s_f) break;
            switch (d1) {
            case 74: p->cutoff = (uint8_t)d2; part_apply_tone(i); break;
            case 71: p->reso = (uint8_t)d2; part_apply_tone(i); break;
            case 73: p->attack = (uint8_t)d2; part_apply_tone(i); break;
            case 72: p->release = (uint8_t)d2; part_apply_tone(i); break;
            case 0:  p->bank = (uint8_t)d2; tsf_channel_midi_control(s_f, i, d1, d2); break;
            case 91: p->rev = (uint8_t)d2; break;
            case 93: p->cho = (uint8_t)d2; break;
            case 120: case 123: p->notes = 0; tsf_channel_midi_control(s_f, i, d1, d2); break;
            default: tsf_channel_midi_control(s_f, i, d1, d2); break;
            }
            break;
        case 0xC:
            p->prog = (uint8_t)(d1 & 0x7F);
            part_apply_program(i);
            break;
        case 0xE:
            if (s_f) tsf_channel_set_pitchwheel(s_f, i, d1 | (d2 << 7));
            break;
        default: break;
        }
    }
}

/* ---- panel events ---- */
static void drain_events(void)
{
    while (s_ev_r != s_ev_w) {
        uint32_t e = s_evq[s_ev_r];
        s_ev_r = (s_ev_r + 1) & 255;
        int kind = e >> 24, ch = (e >> 16) & 0xFF, a = (e >> 8) & 0xFF, b = e & 0xFF;
        if (kind >= SEV_MT_CH && kind <= SEV_MT_MAP) {   /* MT-32 part events */
            if (!mt32_is_open()) continue;
            switch (kind) {
            case SEV_MT_CH: mt32_part_channel(ch, a); break;
            case SEV_MT_PROG: mt32_part_program(ch, a); break;
            case SEV_MT_VOL: mt32_part_cc(ch, 7, a); break;
            case SEV_MT_PAN: mt32_part_cc(ch, 10, a); break;
            default: mt32_apply_map(a); break;
            }
            continue;
        }
        if (g_syn_mode == 1 && (kind == SEV_NOTE_ON || kind == SEV_NOTE_OFF)) {   /* audition notes go to the MT-32 */
            if (mt32_is_open() && ch < 9) mt32_part_note(ch, a, kind == SEV_NOTE_ON ? b : 0);
            continue;
        }
        if (ch >= SYN_PARTS && kind != SEV_MASTER && kind != SEV_ALLOFF && kind != SEV_POLY) continue;
        SynPart *p = &g_parts[ch & 15];
        switch (kind) {
        case SEV_PROG: p->prog = (uint8_t)(a & 0x7F); part_apply_program(ch); break;
        case SEV_BANK: p->bank = (uint8_t)a; part_apply_program(ch); break;
        case SEV_LEVEL: p->level = (uint8_t)a; part_apply_mix(ch); break;
        case SEV_PAN: p->pan = (uint8_t)a; part_apply_mix(ch); break;
        case SEV_CUT: p->cutoff = (uint8_t)a; part_apply_tone(ch); break;
        case SEV_RES: p->reso = (uint8_t)a; part_apply_tone(ch); break;
        case SEV_ATK: p->attack = (uint8_t)a; part_apply_tone(ch); break;
        case SEV_REL: p->release = (uint8_t)a; part_apply_tone(ch); break;
        case SEV_RX: p->rx = (uint8_t)(a > SYN_RX_OFF ? SYN_RX_OFF : a); break;
        case SEV_LOW: p->low = (uint8_t)a; if (p->high < p->low) p->high = p->low; break;
        case SEV_HIGH: p->high = (uint8_t)a; if (p->low > p->high) p->low = p->high; break;
        case SEV_TRANS: p->trans = (int8_t)((a > 48 ? 48 : a) - 24); break;
        case SEV_REV: p->rev = (uint8_t)a; break;
        case SEV_CHO: p->cho = (uint8_t)a; break;
        case SEV_MUTE:
            p->mute = (uint8_t)(a ? 1 : 0);
            part_apply_mix(ch);
            if (p->mute && s_f) tsf_channel_sounds_off_all(s_f, ch);
            break;
        case SEV_MASTER:
            g_syn_master = a;
            if (s_f) tsf_set_output(s_f, TSF_STEREO_INTERLEAVED, OUT_RATE, -6.0f + (a - 100) * 0.12f);
            break;
        case SEV_POLY:
            g_syn_poly = a < 8 ? 8 : a;
            if (s_f) tsf_set_max_voices(s_f, g_syn_poly);
            break;
        case SEV_NOTE_ON:
            if (s_f && !p->mute) tsf_channel_note_on(s_f, ch, a, b / 127.0f);
            if (p->meter < b / 127.0f) p->meter = b / 127.0f;
            break;
        case SEV_NOTE_OFF:
            if (s_f) tsf_channel_note_off(s_f, ch, a);
            break;
        case SEV_ALLOFF:
            if (s_f) for (int c = 0; c < SYN_PARTS; c++) { tsf_channel_sounds_off_all(s_f, c); g_parts[c].notes = 0; }
            mt32_all_off();
            break;
        case SEV_MODE:
            if (a && !mt32_is_open()) break;         /* nothing to switch to */
            if (g_syn_mode != a) {
                if (s_f) for (int c = 0; c < SYN_PARTS; c++) { tsf_channel_sounds_off_all(s_f, c); g_parts[c].notes = 0; }
                mt32_all_off();
                g_syn_mode = a;
            }
            break;
        default: break;
        }
    }
}

/* ---- audio thread ---- */
int syn_audio_thread(unsigned int args, void *argp)
{
    (void)args; (void)argp;
    static short buf[OUT_FRAMES * 2];
    static float bus[SYN_PARTS][OUT_FRAMES * 2];
    static float dry[OUT_FRAMES * 2], revb[OUT_FRAMES * 2], chob[OUT_FRAMES * 2];
    float *bp[SYN_PARTS];
    for (int i = 0; i < SYN_PARTS; i++) bp[i] = bus[i];
    parts_reset();
    fx_defaults(&g_fx);
    fx_init(OUT_RATE);
    if (plat_audio_open() < 0) return -1;
    unsigned int win_max = 0, win_n = 0, win_sum = 0;
    const unsigned int blk_us = OUT_FRAMES * 1000000u / OUT_RATE;
    for (;;) {
        unsigned int t0 = plat_time_us();
        if (s_unload_req) {                    /* unload: silence until the next bank */
            s_retired = s_f;
            s_f = NULL;
            s_ready = 0;
            s_unload_req = 0;
        }
        if (s_pending) {                       /* swap in a new bank between blocks */
            tsf *old = s_f;
            s_f = s_pending;
            s_pending = NULL;
            s_retired = old;
            parts_bind();
            s_ready = 1;
        }
        if (s_perf_pending) perf_take();
        drain_events();
        unsigned char pkt[4];
        int budget = 256;
        if (g_syn_mode == 1 && !mt32_is_open()) g_syn_mode = 0;   /* closed by the panel */
        while (budget-- > 0 && plat_midi_read(pkt)) { if (g_syn_mode == 1) mt32_midi_packet(pkt); else midi_packet(pkt); }
        if (g_syn_mode == 1) {
            mt32_render(buf, OUT_FRAMES);
            static int snap = 0;
            if (++snap >= 3) { snap = 0; mt32_snapshot(); }
        } else if (s_f) {
            tsf_render_parts(s_f, bp, SYN_PARTS, OUT_FRAMES);
            memset(dry, 0, sizeof dry); memset(revb, 0, sizeof revb); memset(chob, 0, sizeof chob);
            for (int i = 0; i < SYN_PARTS; i++) {   /* per-part send levels */
                const float *b = bus[i];
                float rs = g_parts[i].rev / 127.0f, cs = g_parts[i].cho / 127.0f;
                rs *= rs; cs *= cs;
                for (int n = 0; n < OUT_FRAMES * 2; n++) {
                    float v = b[n];
                    dry[n] += v;
                    revb[n] += v * rs;
                    chob[n] += v * cs;
                }
            }
            fx_process(dry, revb, chob, OUT_FRAMES);
            for (int n = 0; n < OUT_FRAMES * 2; n++) {
                float v = dry[n] * 32767.0f;
                buf[n] = (short)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int)v);
            }
        } else memset(buf, 0, sizeof(buf));
        for (int ch = 0; ch < SYN_PARTS; ch++) {   /* meter decay */
            float m = g_parts[ch].meter * 0.985f;
            g_parts[ch].meter = m < 0.002f ? 0.0f : m;
        }
        g_syn_voices = s_f ? tsf_active_voice_count(s_f) : 0;
        unsigned int work = plat_time_us() - t0;
        if (work > win_max) win_max = work;
        win_sum += work;
        if (++win_n >= OUT_RATE / OUT_FRAMES) {
            g_syn_load = (int)(win_max * 100u / blk_us);
            g_syn_load_avg = (int)((unsigned long long)win_sum * 100u / ((unsigned long long)win_n * blk_us));
            win_max = 0; win_n = 0; win_sum = 0;
        }
        plat_audio_write(buf);
    }
    return 0;
}
