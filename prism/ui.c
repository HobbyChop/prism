#include "ui.h"
#include "synth.h"
#include "perf.h"
#include "cfg.h"
#include "fx.h"
#include "mt32.h"
#include <psp2/ctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Fonts g_fonts;

/* palette: charcoal background, hairlines, mint for live elements, amber for
 * meters and peaks */
#define C_BG     0x0F1114
#define C_PANEL  0x171A1F
#define C_PANEL2 0x1E232B
#define C_DEEP   0x0A0C0F
#define C_LINE   0x262B33
#define C_TEXT   0xE8EAEE
#define C_TEXT2  0xB8BEC8
#define C_GREY   0x8B929E
#define C_MINT   0x43C8B5
#define C_MINT2  0x6FDCCC
#define C_AMBER  0xF0B35B
#define C_RED    0xE0574F

enum { PG_PLAY = 0, PG_PERFORM, PG_MIX, PG_FX, PG_MT32, PG_SETUP, PG_COUNT };
static const char *PG_LABEL[PG_COUNT] = { "PLAY", "PERFORM", "MIX", "EFFECTS", "MT-32", "SETUP" };
static const char *GM_FAMILY[16] = { "PIANO", "CHROMATIC", "ORGAN", "GUITAR", "BASS", "STRINGS", "ENSEMBLE", "BRASS",
                                     "REED", "PIPE", "SYNTH LEAD", "SYNTH PAD", "SYNTH FX", "ETHNIC", "PERCUSSIVE", "SOUND FX" };

static int s_page = PG_PLAY;
static int s_part = 0;          /* selected part */
static int s_focus = 0;         /* PLAY: 0 = part rail, 1 = part settings */
static int s_row = 0;           /* PLAY: selected setting */
static int s_aud_note = -1;
static char s_sf_names[32][64];
static int s_sf_count = 0;
static int s_sf_loaded = -1;    /* loaded entry; -1 = built-in */
static const char *s_sf_dir = "ux0:data/prism/soundfonts";
static long s_sf_sizes[32];
static int s_sf_why[32];        /* 0 loads; 1 too big; 2 SF3 compressed; 3 not a SoundFont */
static long s_sf_budget = -1;
static int s_setup_cur = 0, s_setup_list = 0;   /* s_setup_list: the bank the cursor left for the module panel */      /* SETUP: the list, then MASTER, POLY, AUDIO AHEAD */
static int s_page_prev = -1;
/* patch browser: every preset of the bank, by bank then program */
static int s_browse = 0, s_browse_cur = 0, s_bn = 0;
static uint16_t s_bidx[1024];
extern int app_load_bank(const char *path);   /* main.c: load with a boot-style notice */
extern long app_bank_budget(void);            /* main.c: largest bank that can be loaded now */

/* PERFORM */
static int s_perf_col = 0;              /* 0 the zones, 1 the list */
static int s_zone_field = 0;            /* 0 CH 1 LOW 2 HIGH 3 TRANS */
static int s_perf_sel = 0;              /* list cursor: factory first, then user slots */
static int s_perf_cur = 0;              /* loaded entry */
static char s_perf_name[PERF_NAME + 1] = "GM MULTI";
static int s_name_edit = 0, s_name_cur = 0;
static char s_name_buf[PERF_NAME + 1];
static char s_perf_msg[40];
static int s_perf_msg_ms = 0;
#define NAME_CHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+/"
#define PERF_LIST_N (perf_factory_count() + PERF_USER_SLOTS)

/* EFFECTS: the thirteen rack sliders in reading order */
enum { FXS_REV_SIZE = 0, FXS_REV_DAMP, FXS_REV_LEVEL, FXS_CHO_RATE, FXS_CHO_DEPTH, FXS_CHO_LEVEL,
       FXS_EQ_LO, FXS_EQ_MID, FXS_EQ_HI, FXS_LIMITER, FXS_MASTER, FXS_PART_REV, FXS_PART_CHO, FXS_COUNT };
static int s_fx_sel = 0;
/* MT-32: settings rows */
/* MT-32 page: five rows in the MODULE card, five in the EMULATION card */
enum { MR_MODE = 0, MR_REVERB, MR_GAIN, MR_MAP, MR_RESCAN, MR_ANALOG, MR_SRC, MR_RENDER, MR_PARTIALS, MR_CORES, MR_COUNT };
#define MR_MODULE_ROWS 5
static int s_mt_row = 0;
static const char *ANALOG_NAME[4] = { "DIGITAL ONLY", "COARSE", "ACCURATE", "OVERSAMPLED" };
static int fx_get(int k)
{
    switch (k) {
    case FXS_REV_SIZE: return g_fx.rev_size; case FXS_REV_DAMP: return g_fx.rev_damp; case FXS_REV_LEVEL: return g_fx.rev_level;
    case FXS_CHO_RATE: return g_fx.cho_rate; case FXS_CHO_DEPTH: return g_fx.cho_depth; case FXS_CHO_LEVEL: return g_fx.cho_level;
    case FXS_EQ_LO: return g_fx.eq_lo; case FXS_EQ_MID: return g_fx.eq_mid; case FXS_EQ_HI: return g_fx.eq_hi;
    case FXS_LIMITER: return g_fx.limiter ? 127 : 0; case FXS_MASTER: return g_syn_master;
    case FXS_PART_REV: return g_parts[s_part].rev; default: return g_parts[s_part].cho;
    }
}
static void fx_set(int k, int v)
{
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    switch (k) {
    case FXS_REV_SIZE: g_fx.rev_size = (uint8_t)v; break; case FXS_REV_DAMP: g_fx.rev_damp = (uint8_t)v; break;
    case FXS_REV_LEVEL: g_fx.rev_level = (uint8_t)v; break; case FXS_CHO_RATE: g_fx.cho_rate = (uint8_t)v; break;
    case FXS_CHO_DEPTH: g_fx.cho_depth = (uint8_t)v; break; case FXS_CHO_LEVEL: g_fx.cho_level = (uint8_t)v; break;
    case FXS_EQ_LO: g_fx.eq_lo = (uint8_t)v; break; case FXS_EQ_MID: g_fx.eq_mid = (uint8_t)v; break;
    case FXS_EQ_HI: g_fx.eq_hi = (uint8_t)v; break; case FXS_LIMITER: g_fx.limiter = v >= 64; break;
    case FXS_MASTER: syn_event(SEV_MASTER, 0, v, 0); break;
    case FXS_PART_REV: syn_event(SEV_REV, s_part, v, 0); break;
    default: syn_event(SEV_CHO, s_part, v, 0); break;
    }
}
/* slider positions: card x, y and row */
static void fx_slider_pos(int k, int *x, int *y, int *w)
{
    static const int CX[4] = { 16, 330, 644, 644 }, CY[4] = { 58, 58, 58, 260 };
    int card = k < 3 ? 0 : k < 6 ? 0 : k < 9 ? 1 : k < 11 ? 1 : 2;
    int cy = CY[card], row;
    if (k < 3) { cy = 58; row = k; } else if (k < 6) { cy = 260; row = k - 3; }
    else if (k < 9) { cy = 58; row = k - 6; } else if (k < 11) { cy = 260; row = k - 9; }
    else { cy = 58; row = k - 11; }
    *x = CX[card] + 16; *y = cy + 40 + row * 44; *w = 300 - 32;
}

static const char *NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static void note_name(int n, char *b, int len)   /* 60 = C3 */
{
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    snprintf(b, len, "%s%d", NOTE_NAMES[n % 12], n / 12 - 2);
}
static const char *rx_text(int rx, char *b, int len)
{
    if (rx == SYN_RX_OMNI) return "OMNI";
    if (rx >= SYN_RX_OFF) return "OFF";
    snprintf(b, len, "%d", rx + 1);
    return b;
}
static void perf_msg(const char *m) { snprintf(s_perf_msg, sizeof s_perf_msg, "%s", m); s_perf_msg_ms = 120; }
/* top bar message, shown for two seconds */
static char s_toast[40];
static int s_toast_ms = 0;
static int s_lamp_x = 0;   /* x of the MIDI lamp, for the tap target */
static void toast(const char *m) { snprintf(s_toast, sizeof s_toast, "%s", m); s_toast_ms = 120; }
static void name_begin(const char *cur)
{
    memset(s_name_buf, ' ', PERF_NAME); s_name_buf[PERF_NAME] = 0;
    for (int i = 0; i < PERF_NAME && cur[i]; i++) {
        char c = cur[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        s_name_buf[i] = strchr(NAME_CHARS, c) ? c : ' ';
    }
    s_name_cur = 0; s_name_edit = 1;
}
static void name_trim(char *out)
{
    int n = PERF_NAME;
    while (n > 0 && s_name_buf[n - 1] == ' ') n--;
    memcpy(out, s_name_buf, (size_t)n); out[n] = 0;
}

static void sf_rescan(void)
{
    s_sf_count = plat_list_files(s_sf_dir, ".sf2", s_sf_names, 32);
    for (int i = 0; i < s_sf_count; i++) {
        char path[400];
        snprintf(path, sizeof path, "%s/%s", s_sf_dir, s_sf_names[i]);
        s_sf_sizes[i] = plat_file_size(path);
        /* header check: RIFF, sfbk, and the version chunk (major 3 = SF3 with
         * Ogg-compressed samples, which this player cannot decode) */
        s_sf_why[i] = 0;
        unsigned char h[512];
        int n = plat_file_head(path, h, sizeof h);
        if (n < 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "sfbk", 4)) s_sf_why[i] = 3;
        else for (int k = 12; k + 10 <= n; k++)
            if (!memcmp(h + k, "ifil", 4)) { if ((h[k + 8] | (h[k + 9] << 8)) >= 3) s_sf_why[i] = 2; break; }
    }
    s_sf_budget = app_bank_budget();
    for (int i = 0; i < s_sf_count; i++)
        if (!s_sf_why[i] && s_sf_budget >= 0 && s_sf_sizes[i] > s_sf_budget) s_sf_why[i] = 1;
    /* mark the bank in memory, whoever loaded it */
    s_sf_loaded = -1;
    for (int i = 0; i < s_sf_count; i++) if (!strcmp(s_sf_names[i], g_syn_bank_name)) s_sf_loaded = i;
    if (s_setup_cur > s_sf_count + 2) s_setup_cur = 0;
}
/* part settings, one row each, in card order */
enum { PS_PATCH = 0, PS_CH, PS_LEVEL, PS_PAN, PS_CUT, PS_RES, PS_ATK, PS_REL, PS_REV, PS_CHO, PS_TRANS, PS_LOW, PS_HIGH, PS_COUNT };
static const char *PS_LABEL[PS_COUNT] = { "PATCH", "MIDI CHANNEL", "LEVEL", "PAN", "CUTOFF", "RESONANCE", "ATTACK",
                                          "RELEASE", "REVERB SEND", "CHORUS SEND", "TRANSPOSE", "KEY LOW", "KEY HIGH" };
static int ps_max(int r) { return r == PS_CH ? SYN_RX_OFF : r == PS_TRANS ? 48 : 127; }
static int ps_get(const SynPart *p, int r)
{
    switch (r) {
    case PS_PATCH: return p->prog; case PS_CH: return p->rx; case PS_LEVEL: return p->level; case PS_PAN: return p->pan;
    case PS_CUT: return p->cutoff; case PS_RES: return p->reso; case PS_ATK: return p->attack; case PS_REL: return p->release;
    case PS_REV: return p->rev; case PS_CHO: return p->cho; case PS_TRANS: return p->trans + 24; case PS_LOW: return p->low;
    default: return p->high;
    }
}
static void ps_set(int r, int v)
{
    if (v < 0) v = 0;
    if (v > ps_max(r)) v = ps_max(r);
    static const int EV[PS_COUNT] = { SEV_PROG, SEV_RX, SEV_LEVEL, SEV_PAN, SEV_CUT, SEV_RES, SEV_ATK, SEV_REL, SEV_REV, SEV_CHO, SEV_TRANS, SEV_LOW, SEV_HIGH };
    syn_event(EV[r], s_part, v, 0);
}
static void ps_text(const SynPart *p, int r, char *b, int n)
{
    char t[8];
    switch (r) {
    case PS_PATCH: snprintf(b, n, "%03d", p->prog + 1); break;
    case PS_CH: snprintf(b, n, "%s", rx_text(p->rx, t, sizeof t)); break;
    case PS_PAN: if (p->pan == 64) snprintf(b, n, "C"); else if (p->pan < 64) snprintf(b, n, "L%d", 64 - p->pan); else snprintf(b, n, "R%d", p->pan - 64); break;
    case PS_CUT: case PS_ATK: case PS_REL: { int v = ps_get(p, r) - 64; snprintf(b, n, v ? "%+d" : "0", v); break; }
    case PS_TRANS: snprintf(b, n, p->trans ? "%+d" : "0", p->trans); break;
    case PS_LOW: note_name(p->low, b, n); break;
    case PS_HIGH: note_name(p->high, b, n); break;
    default: snprintf(b, n, "%d", ps_get(p, r)); break;
    }
}

void ui_init(void)
{
    sf_rescan();
}

/* browser table: preset indices sorted by bank, then program */
static void browse_build(void)
{
    int n = syn_preset_count();
    if (n > 1024) n = 1024;
    s_bn = n;
    for (int i = 0; i < n; i++) s_bidx[i] = (uint16_t)i;
    for (int i = 1; i < n; i++) {   /* insertion sort; a few hundred entries, once per bank */
        uint16_t k = s_bidx[i];
        int kb, kp; syn_preset_info(k, &kb, &kp, NULL);
        int j = i - 1;
        while (j >= 0) {
            int jb, jp; syn_preset_info(s_bidx[j], &jb, &jp, NULL);
            if (jb < kb || (jb == kb && jp <= kp)) break;
            s_bidx[j + 1] = s_bidx[j]; j--;
        }
        s_bidx[j + 1] = k;
    }
}
static void browse_open(void)
{
    browse_build();
    const SynPart *p = &g_parts[s_part];
    int want_bank = (s_part == 9 && p->bank == 0) ? 128 : p->bank;
    s_browse_cur = 0;
    for (int i = 0; i < s_bn; i++) {
        int b, n; syn_preset_info(s_bidx[i], &b, &n, NULL);
        if (b == want_bank && n == p->prog) { s_browse_cur = i; break; }
    }
    s_browse = 1;
}
static void browse_apply(void)
{
    if (s_browse_cur < 0 || s_browse_cur >= s_bn) return;
    int b, n; syn_preset_info(s_bidx[s_browse_cur], &b, &n, NULL);
    syn_event(SEV_BANK, s_part, b, 0);
    syn_event(SEV_PROG, s_part, n, 0);
}
static void browse_move(int d)
{
    if (!s_bn) return;
    s_browse_cur = (s_browse_cur + s_bn + d) % s_bn;
    browse_apply();
}

/* ---- pieces ------------------------------------------------------------ */
static void topbar(const UiStatus *st, const char *sub)
{
    gfx_fill(0, 43, SCR_W, 1, C_LINE, 255);
    int x = gfx_text(g_fonts.b17, 20, 12, "PRISM", C_TEXT, 255, 4);
    gfx_text(g_fonts.s11, x + 12, 17, sub, C_GREY, 255, 2);
    char b[32];
    int xr = SCR_W - 20;
    snprintf(b, sizeof b, "%d%%%s", st->batt_pct, st->batt_charging ? "+" : "");
    gfx_text_right(g_fonts.mo11, xr, 16, b, C_GREY, 255, 0); xr -= text_w(g_fonts.mo11, b, 0) + 20;
    snprintf(b, sizeof b, "%d/%d", st->voices, st->poly);
    gfx_text_right(g_fonts.mo11, xr, 16, b, C_GREY, 255, 0); xr -= text_w(g_fonts.mo11, b, 0) + 20;
    /* MIDI lamp */
    gfx_text_right(g_fonts.mo11, xr, 16, "MIDI", C_GREY, 255, 0); xr -= text_w(g_fonts.mo11, "MIDI", 0) + 12;
    s_lamp_x = xr - 3;
    gfx_disc(xr - 3, 22, 3, st->midi_link == 3 ? C_MINT : st->midi_link == 2 ? C_GREY : C_AMBER, st->midi_link ? 255 : 140); xr -= 26;
    if (s_toast_ms > 0) {
        s_toast_ms--;
        gfx_text_right(g_fonts.mo11, xr - 8, 16, s_toast, C_AMBER, 255, 0); xr -= text_w(g_fonts.mo11, s_toast, 0) + 28;
    }
    /* bank pill */
    const char *bank = g_syn_bank_name[0] ? g_syn_bank_name : "NO BANK";
    char bn[40]; snprintf(bn, sizeof bn, "%.30s", bank);
    char *dot = strrchr(bn, '.'); if (dot) *dot = 0;
    const char *mode = g_syn_mode == 1 ? "MT-32" : "GM";
    const char *what = g_syn_mode == 1 ? g_mt32.ctrl_desc : bn;
    char wn[34]; snprintf(wn, sizeof wn, "%.30s", what);
    int pw = text_w(g_fonts.mo11, mode, 0) + 10 + text_w(g_fonts.mo11, wn, 0) + 24;
    gfx_frame(xr - pw, 11, pw, 22, 11, g_syn_mode == 1 ? C_MINT : C_LINE, 255);
    gfx_text(g_fonts.mo11, xr - pw + 12, 16, mode, C_TEXT, 255, 0);
    gfx_text(g_fonts.mo11, xr - pw + 12 + text_w(g_fonts.mo11, mode, 0) + 10, 16, wn, C_GREY, 255, 0);
}

static void tabbar(void)
{
    int y0 = SCR_H - 40;
    gfx_fill(0, y0, SCR_W, 1, C_LINE, 255);
    int x = 24;
    for (int i = 0; i < PG_COUNT; i++) {
        int w = text_w(g_fonts.s11, PG_LABEL[i], 2) + 32;
        gfx_text(g_fonts.s11, x + 16, y0 + 15, PG_LABEL[i], i == s_page ? C_TEXT : C_GREY, 255, 2);
        if (i == s_page) gfx_fill(x + 16, SCR_H - 3, w - 32, 2, C_MINT, 255);
        x += w;
    }
}
static int tab_hit(int tx)
{
    int x = 24;
    for (int i = 0; i < PG_COUNT; i++) {
        int w = text_w(g_fonts.s11, PG_LABEL[i], 2) + 32;
        if (tx >= x && tx < x + w) return i;
        x += w;
    }
    return -1;
}

static void slider(int x, int y, int w, int v, int focus)
{
    int fx = x + (w - 10) * v / 127;
    gfx_fill(x, y + 5, w, 3, C_LINE, 255);
    gfx_fill(x, y + 5, fx - x + 5, 3, C_MINT, 255);
    gfx_disc(fx + 5, y + 6, 5, focus ? C_MINT2 : C_TEXT, 255);
    if (focus) gfx_disc(fx + 5, y + 6, 8, C_MINT, 70);
}

static void card(int x, int y, int w, int h)
{
    gfx_round(x, y, w, h, 10, C_PANEL, 255);
    gfx_frame(x, y, w, h, 10, C_LINE, 255);
}

/* ---- PLAY ----------------------------------------------------------------- */
#define RAIL_X 16
#define RAIL_W 262
#define RAIL_Y 58
#define ROW_H 25
#define CARD_X (RAIL_X + RAIL_W + 16)
#define CARD_W (SCR_W - CARD_X - 16 - 212 - 16)
#define RIGHT_X (SCR_W - 16 - 212)

/* Engine meters: the audio thread's worst block over the last second against its
 * deadline, with a held peak, and voices against the cap. */
static void draw_engine_card(const UiStatus *st)
{
    char b[32];
    card(RIGHT_X, RAIL_Y + 256, 212, 104);
    gfx_text(g_fonts.s11, RIGHT_X + 12, RAIL_Y + 268, "ENGINE", C_GREY, 255, 2);
    {
        static int peak = 0, peak_hold = 0;
        int load = st->load_avg < 0 ? 0 : st->load_avg > 100 ? 100 : st->load_avg;   /* bar: mean */
        int pk = st->load < 0 ? 0 : st->load > 100 ? 100 : st->load;                 /* marker: worst */
        if (pk >= peak) { peak = pk; peak_hold = 90; }
        else if (peak_hold > 0) peak_hold--;
        else if (peak > load) peak--;
        int mx = RIGHT_X + 12, mw = 188;
        uint32_t lc = load >= 85 ? C_AMBER : C_MINT;
        gfx_text(g_fonts.s12, mx, RAIL_Y + 286, "Load  mean / worst", C_TEXT, 255, 0);
        snprintf(b, sizeof b, "%d%% / %d%%", load, pk);
        gfx_text_right(g_fonts.mo11, mx + mw, RAIL_Y + 287, b, pk >= 85 ? C_AMBER : C_TEXT, 255, 0);
        gfx_fill(mx, RAIL_Y + 304, mw, 6, C_PANEL2, 255);
        gfx_fill(mx, RAIL_Y + 304, mw * load / 100, 6, lc, 255);
        gfx_fill(mx + mw * peak / 100 - 1, RAIL_Y + 302, 2, 10, C_TEXT, 255);
        gfx_text(g_fonts.s12, mx, RAIL_Y + 318, "Voices", C_TEXT, 255, 0);
        snprintf(b, sizeof b, "%d / %d", st->voices, st->poly);
        gfx_text_right(g_fonts.mo11, mx + mw, RAIL_Y + 319, b, st->voices >= st->poly ? C_AMBER : C_TEXT, 255, 0);
        int vv = st->voices > st->poly ? st->poly : st->voices;
        gfx_fill(mx, RAIL_Y + 336, mw, 6, C_PANEL2, 255);
        gfx_fill(mx, RAIL_Y + 336, st->poly > 0 ? mw * vv / st->poly : 0, 6, C_MINT, 255);
    }
}


/* ---- PLAY in MT-32 mode: eight parts and the rhythm part ---- */
/* The MT-32 assigns channels to parts through its system area (parts on channels
 * 2-9 from the factory). Each part's channel, patch, volume and pan are edited
 * like the GM parts: the channel is written to synth memory by sysex, the rest
 * are channel messages. */
enum { MP_PATCH = 0, MP_CH, MP_VOL, MP_PAN, MP_COUNT };
static int mt_get(int part, int r)
{
    switch (r) {
    case MP_PATCH: return g_mt32.prog[part];
    case MP_CH: return g_mt32.chan[part];
    case MP_VOL: return g_mt32.vol[part];
    default: return g_mt32.pan[part];
    }
}
static void mt_set(int part, int r, int v)
{
    static const int EV[MP_COUNT] = { SEV_MT_PROG, SEV_MT_CH, SEV_MT_VOL, SEV_MT_PAN };
    int mx = r == MP_CH ? 16 : 127;
    if (r == MP_PATCH && part == 8) return;   /* rhythm part has no patch */
    if (v < 0) v = 0;
    if (v > mx) v = mx;
    syn_event(EV[r], part, v, 0);
}
static void mt_browse_move(int d)
{
    s_browse_cur = (s_browse_cur + 128 + d) % 128;
    mt_set(s_part, MP_PATCH, s_browse_cur);
}

static void draw_play_mt32(const UiStatus *st)
{
    char b[64];
    /* rail */
    gfx_text(g_fonts.s11, RAIL_X + 10, RAIL_Y - 16, "MT-32 PARTS", C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo10, RAIL_X + RAIL_W - 10, RAIL_Y - 15, "CH   VOL", C_GREY, 255, 0);
    for (int i = 0; i < 9; i++) {
        int y = RAIL_Y + i * ROW_H;
        int sel = i == s_part, on = (g_mt32.part_mask >> i) & 1, off = g_mt32.chan[i] >= 16;
        if (sel) { gfx_round(RAIL_X, y, RAIL_W, ROW_H - 1, 6, C_PANEL2, 255); gfx_fill(RAIL_X, y + 4, 2, ROW_H - 9, s_focus == 0 ? C_MINT : C_LINE, 255); }
        if (i < 8) snprintf(b, sizeof b, "%02d", i + 1); else snprintf(b, sizeof b, "R");
        gfx_text(g_fonts.mo11, RAIL_X + 12, y + 6, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s13, RAIL_X + 40, y + 4, 130, i < 8 ? (g_mt32.patch[i][0] ? g_mt32.patch[i] : "-") : "Rhythm", off ? C_GREY : sel ? C_TEXT : C_TEXT2, 255);
        if (off) snprintf(b, sizeof b, "OFF"); else snprintf(b, sizeof b, "%d", g_mt32.chan[i] + 1);
        gfx_text_right(g_fonts.mo10, RAIL_X + RAIL_W - 62, y + 7, b, off ? C_AMBER : C_GREY, 255, 0);
        gfx_fill(RAIL_X + RAIL_W - 54, y + 11, 44, 3, C_LINE, 255);
        gfx_fill(RAIL_X + RAIL_W - 54, y + 11, 44 * g_mt32.vol[i] / 127, 3, on ? C_MINT : C_GREY, 255);
    }

    /* patch card */
    int cx = CARD_X, cy = RAIL_Y - 14;
    if (s_part < 8) snprintf(b, sizeof b, "MT-32  -  PART %d  -  PATCH %03d", s_part + 1, g_mt32.prog[s_part] + 1);
    else snprintf(b, sizeof b, "MT-32  -  RHYTHM PART");
    gfx_text(g_fonts.mo11, cx, cy, b, C_GREY, 255, 1);
    gfx_text_fit(g_fonts.b34, cx, cy + 18, CARD_W - 70, s_part < 8 ? (g_mt32.patch[s_part][0] ? g_mt32.patch[s_part] : "-") : "Rhythm", C_TEXT, 255);
    if (s_part < 8) {   /* patch arrows */
        gfx_round(cx + CARD_W - 62, cy + 22, 28, 28, 6, C_PANEL2, 255);
        gfx_text_center(g_fonts.b17, cx + CARD_W - 48, cy + 27, "<", C_TEXT, 255, 0);
        gfx_round(cx + CARD_W - 30, cy + 22, 28, 28, 6, C_PANEL2, 255);
        gfx_text_center(g_fonts.b17, cx + CARD_W - 16, cy + 27, ">", C_TEXT, 255, 0);
    }
    /* settings */
    int ly = cy + 72, vx = cx + 124, sw = CARD_W - 124 - 54;
    gfx_fill(cx, ly - 12, CARD_W, 1, C_LINE, 255);
    static const char *LBL[MP_COUNT] = { "PATCH", "CHANNEL", "VOLUME", "PAN" };
    for (int r = 0; r < MP_COUNT; r++) {
        int y = ly + r * 22, sel = s_focus == 1 && r == s_row;
        if (sel) gfx_round(cx - 8, y - 3, CARD_W + 16, 22, 5, C_PANEL2, 255);
        gfx_text(g_fonts.s11, cx, y + 3, LBL[r], sel ? C_TEXT : C_GREY, 255, 2);
        int v = mt_get(s_part, r);
        if (r == MP_PATCH) {
            if (s_part < 8) {
                snprintf(b, sizeof b, "%03d", v + 1);
                gfx_text(g_fonts.mo11, vx, y + 2, b, C_MINT2, 255, 0);
                gfx_text(g_fonts.mo10, vx + 40, y + 3, "ONE OF THE 128  -  TRIANGLE LISTS THEM", C_GREY, 255, 0);
            } else gfx_text(g_fonts.mo11, vx, y + 2, "-", C_GREY, 255, 0);
        } else if (r == MP_CH) {
            if (v >= 16) snprintf(b, sizeof b, "OFF"); else snprintf(b, sizeof b, "%d", v + 1);
            gfx_text(g_fonts.mo11, vx, y + 2, b, v >= 16 ? C_AMBER : C_MINT2, 255, 0);
            gfx_text(g_fonts.mo10, vx + 40, y + 3, v >= 16 ? "RECEIVES NOTHING" : "RECEIVES THIS CHANNEL", C_GREY, 255, 0);
        } else {
            int fx = vx + (sw - 8) * v / 127;
            gfx_fill(vx, y + 7, sw, 3, C_LINE, 255);
            gfx_fill(vx, y + 7, fx - vx + 4, 3, C_MINT, 255);
            gfx_disc(fx + 4, y + 8, 4, sel ? C_MINT2 : C_TEXT, 255);
            if (r == MP_PAN) { if (v == 64) snprintf(b, sizeof b, "C"); else if (v < 64) snprintf(b, sizeof b, "L%d", 64 - v); else snprintf(b, sizeof b, "R%d", v - 64); }
            else snprintf(b, sizeof b, "%d", v);
            gfx_text_right(g_fonts.mo11, cx + CARD_W, y + 2, b, C_TEXT, 255, 0);
        }
    }
    gfx_text(g_fonts.s12, cx, ly + MP_COUNT * 22 + 14, "A game that sets its own channels or uploads its own timbres", C_TEXT2, 255, 0);
    gfx_text(g_fonts.s12, cx, ly + MP_COUNT * 22 + 30, "is shown here as it left things.", C_TEXT2, 255, 0);

    /* right column: module and engine */
    card(RIGHT_X, RAIL_Y - 14, 212, 150);
    gfx_text(g_fonts.s11, RIGHT_X + 12, RAIL_Y - 2, "MT-32", C_GREY, 255, 2);
    gfx_round(RIGHT_X + 12, RAIL_Y + 14, 188, 30, 6, C_DEEP, 255);
    gfx_text_fit(g_fonts.mo14, RIGHT_X + 18, RAIL_Y + 20, 176, g_mt32.lcd[0] ? g_mt32.lcd : "", C_MINT, 255);
    snprintf(b, sizeof b, "%d of %d partials", g_mt32.partials, g_mt32.partials_max);
    gfx_text(g_fonts.mo10, RIGHT_X + 12, RAIL_Y + 54, b, C_GREY, 255, 0);
    gfx_text_fit(g_fonts.mo10, RIGHT_X + 12, RAIL_Y + 70, 188, g_mt32.ctrl_desc, C_GREY, 255);
    snprintf(b, sizeof b, "Reverb %s  -  gain %d", g_mt32.reverb_on ? "on" : "off", g_mt32.gain);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 92, b, C_TEXT2, 255, 0);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 108, g_mt32.map_stock ? "Channels 2 to 9, rhythm 10." : "Channels 1 to 8, rhythm 10.", C_TEXT2, 255, 0);
    draw_engine_card(st);
    gfx_text(g_fonts.mo10, CARD_X, SCR_H - 58, s_focus == 0
             ? "UP DOWN PART   O + LEFT RIGHT PATCH   RIGHT SETTINGS   TRIANGLE LIST   X AUDITION"
             : "UP DOWN SETTING   O + LEFT RIGHT VALUE   LEFT BACK TO PARTS   TRIANGLE LIST   X AUDITION", C_GREY, 255, 0);
}

static void draw_play(const UiStatus *st)
{
    if (g_syn_mode == 1 && mt32_is_open()) { draw_play_mt32(st); return; }
    (void)st;
    /* part rail */
    gfx_text(g_fonts.s11, RAIL_X + 10, RAIL_Y - 16, "PARTS", C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo10, RAIL_X + RAIL_W - 10, RAIL_Y - 15, "CH   LVL", C_GREY, 255, 0);
    for (int i = 0; i < SYN_PARTS; i++) {
        const SynPart *p = &g_parts[i];
        int y = RAIL_Y + i * ROW_H;
        int sel = i == s_part;
        if (sel) { gfx_round(RAIL_X, y, RAIL_W, ROW_H - 1, 6, C_PANEL2, 255); gfx_fill(RAIL_X, y + 4, 2, ROW_H - 9, s_focus == 0 ? C_MINT : C_LINE, 255); }
        char b[8];
        snprintf(b, sizeof b, "%02d", i + 1);
        gfx_text(g_fonts.mo11, RAIL_X + 12, y + 6, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s13, RAIL_X + 40, y + 4, 130, p->name[0] ? p->name : "-", p->mute ? C_GREY : sel ? C_TEXT : C_TEXT2, 255);
        {
            char rb[8];
            gfx_text_right(g_fonts.mo10, RAIL_X + RAIL_W - 62, y + 7, rx_text(p->rx, rb, sizeof rb), p->rx >= SYN_RX_OFF ? C_AMBER : C_GREY, 255, 0);
        }
        int mw = (int)(p->meter * 44.0f);
        gfx_fill(RAIL_X + RAIL_W - 54, y + 11, 44, 3, C_LINE, 255);
        if (mw > 0) gfx_fill(RAIL_X + RAIL_W - 54, y + 11, mw, 3, p->meter > 0.8f ? C_AMBER : C_MINT, 255);
        if (p->mute) gfx_text(g_fonts.mo10, RAIL_X + RAIL_W - 54, y + 7, "MUTE", C_AMBER, 255, 0);
    }

    /* patch card */
    const SynPart *p = &g_parts[s_part];
    char b[64];
    int cx = CARD_X, cy = RAIL_Y - 14;
    if (s_part == 9 && p->bank == 0) snprintf(b, sizeof b, "DRUMS  -  KIT %03d", p->prog);
    else snprintf(b, sizeof b, "%s  -  BANK %03d  -  PC %03d", GM_FAMILY[(p->prog >> 3) & 15], p->bank, p->prog + 1);
    gfx_text(g_fonts.mo11, cx, cy, b, C_GREY, 255, 1);
    gfx_text_fit(g_fonts.b34, cx, cy + 18, CARD_W - 70, p->name[0] ? p->name : "No bank loaded", C_TEXT, 255);
    /* patch arrows */
    gfx_round(cx + CARD_W - 62, cy + 22, 28, 28, 6, C_PANEL2, 255);
    gfx_text_center(g_fonts.b17, cx + CARD_W - 48, cy + 27, "<", C_TEXT, 255, 0);
    gfx_round(cx + CARD_W - 30, cy + 22, 28, 28, 6, C_PANEL2, 255);
    gfx_text_center(g_fonts.b17, cx + CARD_W - 16, cy + 27, ">", C_TEXT, 255, 0);
    /* settings of the part, one row each; O moves the cursor in */
    {
        int ly = cy + 72, vx = cx + 124, sw = CARD_W - 124 - 54;
        for (int r = 0; r < PS_COUNT; r++) {
            int y = ly + r * 22;
            int sel = s_focus == 1 && r == s_row;
            if (sel) { gfx_round(cx - 8, y - 3, CARD_W + 16, 21, 5, C_PANEL2, 255); gfx_fill(cx - 8, y + 1, 2, 13, C_MINT, 255); }
            gfx_text(g_fonts.s11, cx, y + 2, PS_LABEL[r], sel ? C_TEXT : C_GREY, 255, 2);
            ps_text(p, r, b, sizeof b);
            if (r == PS_PATCH) {
                gfx_text_fit(g_fonts.s12, vx, y, sw, p->name[0] ? p->name : "-", C_TEXT, 255);
                gfx_text_right(g_fonts.mo11, cx + CARD_W, y + 2, b, C_GREY, 255, 0);
            } else if (r == PS_CH) {
                gfx_text(g_fonts.mo11, vx, y + 2, b, p->rx >= SYN_RX_OFF ? C_AMBER : C_MINT2, 255, 0);
                gfx_text(g_fonts.mo10, vx + 40, y + 3, p->rx == SYN_RX_OMNI ? "EVERY CHANNEL" : p->rx >= SYN_RX_OFF ? "RECEIVES NOTHING" : "RECEIVES THIS CHANNEL", C_GREY, 255, 0);
            } else {
                int v = ps_get(p, r), mx = ps_max(r);
                int fx = vx + (sw - 8) * v / mx;
                gfx_fill(vx, y + 7, sw, 3, C_LINE, 255);
                gfx_fill(vx, y + 7, fx - vx + 4, 3, C_MINT, 255);
                gfx_disc(fx + 4, y + 8, 4, sel ? C_MINT2 : C_TEXT, 255);
                gfx_text_right(g_fonts.mo11, cx + CARD_W, y + 2, b, C_TEXT, 255, 0);
            }
        }
    }

    /* right column: the rig and the effects */
    card(RIGHT_X, RAIL_Y - 14, 212, 150);
    gfx_text(g_fonts.s11, RIGHT_X + 12, RAIL_Y - 2, "RIG", C_GREY, 255, 2);
    int n = 0;
    for (int i = 0; i < SYN_PARTS; i++) if (g_parts[i].name[0] && !g_parts[i].mute && (g_parts[i].notes || g_parts[i].meter > 0.02f)) n++;
    snprintf(b, sizeof b, "%d part%s sounding", n, n == 1 ? "" : "s");
    gfx_text(g_fonts.m15, RIGHT_X + 12, RAIL_Y + 14, b, C_TEXT, 255, 0);
    /* keyboard strip: the selected part's range */
    for (int i = 0; i < 24; i++) {
        int n0 = i * 128 / 24, n1 = (i + 1) * 128 / 24 - 1;
        int in = !(n1 < p->low || n0 > p->high) && p->rx < SYN_RX_OFF;
        gfx_fill(RIGHT_X + 12 + i * 8, RAIL_Y + 42, 6, 24, C_PANEL2, 255);
        gfx_fill(RIGHT_X + 12 + i * 8, RAIL_Y + 63, 6, 3, in ? C_MINT : C_LINE, 255);
    }
    {
        char lo[8], hi[8], rb[8];
        note_name(p->low, lo, sizeof lo); note_name(p->high, hi, sizeof hi);
        snprintf(b, sizeof b, "PART %d  CH %s  %s..%s  TR %+d", s_part + 1, rx_text(p->rx, rb, sizeof rb), lo, hi, p->trans);
        gfx_text(g_fonts.mo10, RIGHT_X + 12, RAIL_Y + 74, b, C_GREY, 255, 0);
    }
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 96, s_perf_name, C_TEXT, 255, 0);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 112, "is the performance in play.", C_TEXT2, 255, 0);
    card(RIGHT_X, RAIL_Y + 148, 212, 96);
    gfx_text(g_fonts.s11, RIGHT_X + 12, RAIL_Y + 160, "EFFECTS", C_GREY, 255, 2);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 178, "Reverb send", C_TEXT, 255, 0);
    snprintf(b, sizeof b, "%d  LEVEL %d", p->rev, g_fx.rev_level);
    gfx_text_right(g_fonts.mo11, RIGHT_X + 200, RAIL_Y + 179, b, C_GREY, 255, 0);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 196, "Chorus send", C_TEXT, 255, 0);
    snprintf(b, sizeof b, "%d  LEVEL %d", p->cho, g_fx.cho_level);
    gfx_text_right(g_fonts.mo11, RIGHT_X + 200, RAIL_Y + 197, b, C_GREY, 255, 0);
    gfx_text(g_fonts.s12, RIGHT_X + 12, RAIL_Y + 214, "Limiter", C_TEXT, 255, 0);
    gfx_text_right(g_fonts.mo11, RIGHT_X + 200, RAIL_Y + 215, g_fx.limiter ? "ON" : "OFF", g_fx.limiter ? C_MINT : C_GREY, 255, 0);
    draw_engine_card(st);
    /* hint line */
    gfx_text(g_fonts.mo10, CARD_X, SCR_H - 58, s_focus == 0
             ? "UP DOWN PART   O + LEFT RIGHT PATCH   RIGHT SETTINGS   TRIANGLE BROWSE   X AUDITION   SQUARE MUTE"
             : "UP DOWN SETTING   O + LEFT RIGHT VALUE   LEFT BACK TO PARTS   TRIANGLE BROWSE   X AUDITION", C_GREY, 255, 0);
}

#define BR_X 292
#define BR_Y 50
#define BR_W 420
#define BR_ROWS 26
#define BR_ROW_H 16
static void draw_browser(void)
{
    int h = BR_ROWS * BR_ROW_H + 52;
    gfx_round(BR_X, BR_Y, BR_W, h, 10, C_DEEP, 250);
    gfx_frame(BR_X, BR_Y, BR_W, h, 10, C_LINE, 255);
    char b[48];
    snprintf(b, sizeof b, "PATCHES  PART %02d", s_part + 1);
    gfx_text(g_fonts.s11, BR_X + 16, BR_Y + 12, b, C_GREY, 255, 2);
    snprintf(b, sizeof b, "%d IN %s", s_bn, g_syn_bank_name);
    gfx_text_right(g_fonts.mo10, BR_X + BR_W - 16, BR_Y + 13, b, C_GREY, 255, 0);
    int first = s_browse_cur - BR_ROWS / 2;
    if (first > s_bn - BR_ROWS) first = s_bn - BR_ROWS;
    if (first < 0) first = 0;
    for (int r = 0; r < BR_ROWS && first + r < s_bn; r++) {
        int i = first + r, y = BR_Y + 34 + r * BR_ROW_H;
        int bank, num; const char *name;
        syn_preset_info(s_bidx[i], &bank, &num, &name);
        int cur = i == s_browse_cur;
        if (cur) { gfx_round(BR_X + 8, y - 1, BR_W - 16, BR_ROW_H, 4, C_PANEL2, 255); gfx_fill(BR_X + 8, y + 2, 2, BR_ROW_H - 6, C_MINT, 255); }
        snprintf(b, sizeof b, "%3d:%03d", bank, num + 1);
        gfx_text(g_fonts.mo10, BR_X + 18, y + 2, b, bank == 128 ? C_AMBER : C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s12, BR_X + 78, y, BR_W - 100, name ? name : "", cur ? C_TEXT : C_TEXT2, 255);
    }
    gfx_text(g_fonts.mo10, BR_X + 16, BR_Y + h - 16, "UP DOWN OR DRAG   PLAYS AS YOU GO   O CLOSE", C_GREY, 255, 0);
}

static void draw_browser_mt32(void)
{
    int h = BR_ROWS * BR_ROW_H + 52;
    gfx_round(BR_X, BR_Y, BR_W, h, 10, C_DEEP, 250);
    gfx_frame(BR_X, BR_Y, BR_W, h, 10, C_LINE, 255);
    char b[48];
    snprintf(b, sizeof b, "PATCHES  PART %d", s_part + 1);
    gfx_text(g_fonts.s11, BR_X + 16, BR_Y + 12, b, C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo10, BR_X + BR_W - 16, BR_Y + 13, "THE MT-32'S 128", C_GREY, 255, 0);
    int first = s_browse_cur - BR_ROWS / 2;
    if (first > 128 - BR_ROWS) first = 128 - BR_ROWS;
    if (first < 0) first = 0;
    for (int r = 0; r < BR_ROWS && first + r < 128; r++) {
        int i = first + r, y = BR_Y + 34 + r * BR_ROW_H, cur = i == s_browse_cur;
        if (cur) { gfx_round(BR_X + 8, y - 1, BR_W - 16, BR_ROW_H, 4, C_PANEL2, 255); gfx_fill(BR_X + 8, y + 2, 2, BR_ROW_H - 6, C_MINT, 255); }
        snprintf(b, sizeof b, "%03d", i + 1);
        gfx_text(g_fonts.mo10, BR_X + 18, y + 2, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s12, BR_X + 78, y, BR_W - 100, g_mt32.pname[i][0] ? g_mt32.pname[i] : "-", cur ? C_TEXT : C_TEXT2, 255);
    }
    gfx_text(g_fonts.mo10, BR_X + 16, BR_Y + h - 16, "UP DOWN OR DRAG   PLAYS AS YOU GO   O CLOSE", C_GREY, 255, 0);
}

/* ---- MIX -------------------------------------------------------------------- */
#define MIX_X 16
#define MIX_Y 58
#define STRIP_W 50
#define FADER_H 244

static void draw_mix(const UiStatus *st)
{
    (void)st;
    char b[16];
    for (int i = 0; i < SYN_PARTS; i++) {
        const SynPart *p = &g_parts[i];
        int x = MIX_X + i * (STRIP_W + 2);
        int sel = i == s_part;
        if (sel) gfx_round(x, MIX_Y - 6, STRIP_W, FADER_H + 120, 8, C_PANEL, 255);
        snprintf(b, sizeof b, "%02d", i + 1);
        gfx_text_center(g_fonts.mo10, x + STRIP_W / 2, MIX_Y + 2, b, C_GREY, 255, 0);
        /* pan */
        int pcx = x + STRIP_W / 2, pcy = MIX_Y + 30;
        gfx_frame(pcx - 13, pcy - 13, 26, 26, 13, C_LINE, 255);
        int shift = ((int)p->pan - 64) * 9 / 64;
        gfx_fill(pcx - 1 + shift, pcy - 10, 2, 8, C_TEXT, 255);
        /* fader and meter */
        int fy = MIX_Y + 52;
        int mh = (int)(p->meter * FADER_H);
        gfx_fill(x + 10, fy, 6, FADER_H, C_PANEL2, 255);
        if (mh > 0) gfx_fill(x + 10, fy + FADER_H - mh, 6, mh, p->meter > 0.8f ? C_AMBER : C_MINT, 255);
        int lv = p->mute ? 0 : p->level;
        int ky = fy + FADER_H - (FADER_H - 8) * lv / 127 - 8;
        gfx_fill(x + 28, fy, 3, FADER_H, C_LINE, 255);
        gfx_fill(x + 28, ky + 4, 3, fy + FADER_H - ky - 4, C_MINT, 255);
        gfx_round(x + 20, ky, 19, 8, 3, sel ? C_MINT2 : C_TEXT, 255);
        /* readouts */
        float db = (lv <= 0) ? -60.0f : 40.0f * __builtin_log10f(lv / 100.0f);
        if (db <= -60.0f) snprintf(b, sizeof b, "-inf"); else snprintf(b, sizeof b, "%+.1f", db);
        gfx_text_center(g_fonts.mo10, x + STRIP_W / 2, fy + FADER_H + 8, b, C_TEXT, 255, 0);
        gfx_frame(x + 12, fy + FADER_H + 26, 26, 18, 4, p->mute ? C_AMBER : C_LINE, 255);
        gfx_text_center(g_fonts.mo10, x + STRIP_W / 2, fy + FADER_H + 30, "M", p->mute ? C_AMBER : C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s11, x + 3, fy + FADER_H + 50, STRIP_W - 6, p->name[0] ? p->name : "-", C_TEXT2, 255);
    }
    /* master */
    int mx = SCR_W - 16 - 96;
    gfx_fill(mx - 12, MIX_Y - 6, 1, FADER_H + 120, C_LINE, 255);
    gfx_text(g_fonts.s11, mx, MIX_Y + 2, "MASTER", C_GREY, 255, 2);
    int fy = MIX_Y + 30, mh2 = FADER_H + 22;
    float pk = 0; for (int i = 0; i < SYN_PARTS; i++) if (g_parts[i].meter > pk) pk = g_parts[i].meter;
    int ph = (int)(pk * mh2);
    gfx_fill(mx, fy, 8, mh2, C_PANEL2, 255); if (ph) gfx_fill(mx, fy + mh2 - ph, 8, ph, pk > 0.8f ? C_AMBER : C_MINT, 255);
    gfx_fill(mx + 12, fy, 8, mh2, C_PANEL2, 255); if (ph) gfx_fill(mx + 12, fy + mh2 - ph, 8, ph, pk > 0.8f ? C_AMBER : C_MINT, 255);
    int ky = fy + mh2 - (mh2 - 9) * g_syn_master / 127 - 9;
    gfx_fill(mx + 34, fy, 3, mh2, C_LINE, 255);
    gfx_fill(mx + 34, ky + 4, 3, fy + mh2 - ky - 4, C_MINT, 255);
    gfx_round(mx + 25, ky, 22, 9, 3, s_part == SYN_PARTS ? C_MINT2 : C_TEXT, 255);
    snprintf(b, sizeof b, "%d", g_syn_master);
    gfx_text(g_fonts.mo12, mx + 52, fy + mh2 / 2 - 6, b, C_TEXT, 255, 0);
    gfx_text(g_fonts.mo10, MIX_X, SCR_H - 58, "LEFT RIGHT STRIP   O + UP DOWN LEVEL   SQUARE MUTE   TOUCH A FADER TO DRAG IT", C_GREY, 255, 0);
}

/* ---- SETUP ------------------------------------------------------------------- */
#define SU_LIST_X 16
#define SU_LIST_W 440
#define SU_LIST_Y 58
#define SU_ROW_H 24
#define SU_ROWS 15
static int setup_items(void) { return s_sf_count + 1 + 3; }   /* banks, MASTER, POLY, AUDIO AHEAD */

static void draw_setup(const UiStatus *st)
{
    char b[96];
    /* banks */
    card(SU_LIST_X, SU_LIST_Y - 12, SU_LIST_W, SU_ROWS * SU_ROW_H + 40);
    gfx_text(g_fonts.s11, SU_LIST_X + 16, SU_LIST_Y, "SOUNDFONTS", C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo10, SU_LIST_X + SU_LIST_W - 16, SU_LIST_Y + 1, "ux0:data/prism/soundfonts", C_GREY, 255, 0);
    int nb = s_sf_count + 1;
    int first = (s_setup_cur < nb ? s_setup_cur : 0) - SU_ROWS / 2;
    if (first > nb - SU_ROWS) first = nb - SU_ROWS;
    if (first < 0) first = 0;
    for (int r = 0; r < SU_ROWS && first + r < nb; r++) {
        int i = first + r, y = SU_LIST_Y + 22 + r * SU_ROW_H;
        int cur = i == s_setup_cur, loaded = (i == s_sf_loaded + 1);
        if (cur) { gfx_round(SU_LIST_X + 8, y - 2, SU_LIST_W - 16, SU_ROW_H - 2, 6, C_PANEL2, 255); gfx_fill(SU_LIST_X + 8, y + 3, 2, SU_ROW_H - 12, C_MINT, 255); }
        if (i == 0) {
            gfx_text(g_fonts.s13, SU_LIST_X + 22, y + 2, "GeneralUser GS", cur ? C_TEXT : C_TEXT2, 255, 0);
            gfx_text_right(g_fonts.mo10, SU_LIST_X + SU_LIST_W - 40, y + 5, "BUILT IN  31 MB", C_GREY, 255, 0);
        } else {
            int why = s_sf_why[i - 1];
            gfx_text_fit(g_fonts.s13, SU_LIST_X + 22, y + 2, SU_LIST_W - 200, s_sf_names[i - 1], why ? C_RED : cur ? C_TEXT : C_TEXT2, 255);
            long mb = s_sf_sizes[i - 1] > 0 ? (s_sf_sizes[i - 1] + 524288) / 1048576 : 0;
            if (why == 1) snprintf(b, sizeof b, "TOO BIG  %ld MB", mb);
            else if (why == 2) snprintf(b, sizeof b, "SF3 COMPRESSED  %ld MB", mb);
            else if (why == 3) snprintf(b, sizeof b, "NOT A SOUNDFONT");
            else snprintf(b, sizeof b, "%ld MB", mb);
            gfx_text_right(g_fonts.mo10, SU_LIST_X + SU_LIST_W - 40, y + 5, b, why ? C_RED : C_GREY, 255, 0);
        }
        if (loaded) gfx_disc(SU_LIST_X + SU_LIST_W - 22, y + 9, 3, C_MINT, 255);
    }
    if (s_sf_count == 0) gfx_text(g_fonts.s12, SU_LIST_X + 22, SU_LIST_Y + 22 + SU_ROW_H + 2, "Copy .sf2 files into the folder above; they appear here.", C_GREY, 255, 0);
    if (s_sf_budget >= 0) snprintf(b, sizeof b, "X LOADS IT.  ROOM FOR A BANK OF %ld MB.  RED ONES CANNOT LOAD.  A STATE SAVED ON PERFORM KEEPS ITS BANK.", s_sf_budget >> 20);
    else snprintf(b, sizeof b, "X LOADS IT.  RED ONES CANNOT LOAD.  A STATE SAVED ON PERFORM KEEPS ITS BANK.");
    gfx_text(g_fonts.mo10, SU_LIST_X + 16, SU_LIST_Y + SU_ROWS * SU_ROW_H + 12, b, C_GREY, 255, 0);

    /* module */
    int mx = SU_LIST_X + SU_LIST_W + 16, mw = SCR_W - 16 - mx;
    card(mx, SU_LIST_Y - 12, mw, 176);
    gfx_text(g_fonts.s11, mx + 16, SU_LIST_Y, "MODULE", C_GREY, 255, 2);
    int sel = s_setup_cur == nb;
    gfx_text(g_fonts.s11, mx + 16, SU_LIST_Y + 24, "MASTER", sel ? C_TEXT : C_GREY, 255, 2);
    snprintf(b, sizeof b, "%d", g_syn_master);
    gfx_text_right(g_fonts.mo14, mx + mw - 16, SU_LIST_Y + 21, b, C_TEXT, 255, 0);
    slider(mx + 16, SU_LIST_Y + 42, mw - 32, g_syn_master, sel);
    sel = s_setup_cur == nb + 1;
    gfx_text(g_fonts.s11, mx + 16, SU_LIST_Y + 68, "POLYPHONY", sel ? C_TEXT : C_GREY, 255, 2);
    snprintf(b, sizeof b, "%d", g_syn_poly);
    gfx_text_right(g_fonts.mo14, mx + mw - 16, SU_LIST_Y + 65, b, C_TEXT, 255, 0);
    slider(mx + 16, SU_LIST_Y + 86, mw - 32, g_syn_poly, sel);
    sel = s_setup_cur == nb + 2;
    gfx_text(g_fonts.s11, mx + 16, SU_LIST_Y + 112, "AUDIO AHEAD", sel ? C_TEXT : C_GREY, 255, 2);
    snprintf(b, sizeof b, "%d BLOCKS  %d MS", plat_audio_ahead(), plat_audio_ahead() * 1000 * OUT_FRAMES / OUT_RATE);
    gfx_text_right(g_fonts.mo14, mx + mw - 16, SU_LIST_Y + 109, b, sel ? C_MINT2 : C_TEXT, 255, 0);
    gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 134, "MORE COVERS A SLOW BLOCK   LESS IS TIGHTER FOR LIVE KEYS", C_GREY, 255, 0);
    gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 150, s_setup_cur >= nb ? "LEFT BACK TO THE LIST   O + PAD CHANGES" : "RIGHT TO THIS PANEL", C_GREY, 255, 0);

    card(mx, SU_LIST_Y + 176, mw, 124);
    gfx_text(g_fonts.s11, mx + 16, SU_LIST_Y + 188, "STATUS", C_GREY, 255, 2);
    {
        const char *line; unsigned int col = C_TEXT;
        if (st->midi_stale) { line = "Stale kernel module: reboot the Vita"; col = C_AMBER; }
        else if (st->midi_link == 0) { line = "MIDI driver not running"; col = C_AMBER; }
        else if (st->midi_link == 1) { line = "USB port lost"; col = C_AMBER; }
        else if (st->midi_link == 3) { line = "Adapter connected"; col = C_MINT; }
        else line = "Waiting for the adapter";
        gfx_text(g_fonts.m15, mx + 16, SU_LIST_Y + 208, line, col, 255, 0);
        char d[96];
        d[0] = 0;
        if (st->midi_link <= 1 && !st->midi_stale) {
            if (st->fail_phase > 0 && st->retry_s >= 0) snprintf(d, sizeof d, "takeover failed at step %d (%08X): retry in %d s", st->fail_phase, st->fail_err, st->retry_s);
            else if (st->fail_phase > 0) snprintf(d, sizeof d, "takeover failed at step %d (%08X): hold SELECT or tap the lamp", st->fail_phase, st->fail_err);
            else snprintf(d, sizeof d, "hold SELECT, or tap the MIDI lamp, to reconnect");
        } else if (st->midi_link == 2) snprintf(d, sizeof d, "hold SELECT to re-present the device to the adapter");
        if (d[0]) gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 280, d, st->midi_link <= 1 ? C_AMBER : C_GREY, 255, 0);
    }
    snprintf(b, sizeof b, "engine load %d%% mean  %d%% worst   %d of %d voices", st->load_avg, st->load, st->voices, st->poly);
    gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 232, b, C_GREY, 255, 0);
    snprintf(b, sizeof b, "bank in memory: %.40s", g_syn_bank_name[0] ? g_syn_bank_name : "none");
    gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 248, b, C_GREY, 255, 0);
    gfx_text(g_fonts.mo10, mx + 16, SU_LIST_Y + 264, "TinySoundFont MIT   Munt LGPL   GeneralUser GS", C_GREY, 255, 0);
}

/* ---- PERFORM ---------------------------------------------------------------- */
#define ZONE_Y 58
#define ZONE_H 22
#define BAR_X 224
#define BAR_W 400
#define LIST_Y 116
#define LIST_ROWS 17

static void draw_perform(const UiStatus *st)
{
    (void)st;
    char b[40], b2[16];
    /* zones */
    gfx_text(g_fonts.s11, 28, 42, "PART", C_GREY, 255, 2);
    gfx_text(g_fonts.s11, 178, 42, "CH", C_GREY, 255, 2);
    gfx_text(g_fonts.s11, BAR_X, 42, "RANGE", C_GREY, 255, 2);
    gfx_text(g_fonts.s11, 636, 42, "TRANS", C_GREY, 255, 2);
    for (int i = 0; i < SYN_PARTS; i++) {
        const SynPart *p = &g_parts[i];
        int y = ZONE_Y + i * ZONE_H;
        int sel = i == s_part, off = p->rx >= SYN_RX_OFF;
        if (sel) { gfx_round(16, y, 700, ZONE_H - 1, 6, C_PANEL2, 255); gfx_fill(16, y + 4, 2, ZONE_H - 9, C_MINT, 255); }
        snprintf(b, sizeof b, "%02d", i + 1);
        gfx_text(g_fonts.mo11, 28, y + 4, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s12, 56, y + 3, 112, p->name[0] ? p->name : "-", off ? C_GREY : sel ? C_TEXT : C_TEXT2, 255);
        int fs = sel && s_perf_col == 0;
        int f0 = fs && s_zone_field == 0, f1 = fs && s_zone_field == 1, f2 = fs && s_zone_field == 2, f3 = fs && s_zone_field == 3;
        gfx_text(g_fonts.mo11, 178, y + 4, rx_text(p->rx, b2, sizeof b2), f0 ? C_MINT2 : off ? C_AMBER : C_TEXT, 255, 0);
        if (f0) gfx_fill(178, y + ZONE_H - 3, 30, 1, C_MINT, 255);
        /* range bar: octave ticks, middle C marked, the range lit */
        gfx_fill(BAR_X, y + 5, BAR_W, 12, C_PANEL2, 255);
        for (int n = 12; n < 128; n += 12) gfx_fill(BAR_X + n * BAR_W / 128, y + 5, 1, 12, n == 60 ? C_GREY : C_LINE, 255);
        {
            int x0 = BAR_X + p->low * BAR_W / 128, x1 = BAR_X + (p->high + 1) * BAR_W / 128;
            if (x1 <= x0) x1 = x0 + 1;
            gfx_fill(x0, y + 5, x1 - x0, 12, off ? C_LINE : p->mute ? C_GREY : C_MINT, sel ? 255 : 170);
            if (sel) {
                gfx_fill(x0, y + 3, 2, 16, f1 ? C_TEXT : C_MINT2, 255);
                gfx_fill(x1 - 2, y + 3, 2, 16, f2 ? C_TEXT : C_MINT2, 255);
            }
        }
        if (sel) {
            char lo[8], hi[8];
            note_name(p->low, lo, sizeof lo); note_name(p->high, hi, sizeof hi);
            gfx_text_right(g_fonts.mo10, BAR_X - 6, y + 5, lo, f1 ? C_MINT2 : C_GREY, 255, 0);
            gfx_text(g_fonts.mo10, BAR_X + BAR_W + 6, y + 5, hi, f2 ? C_MINT2 : C_GREY, 255, 0);
        }
        snprintf(b, sizeof b, "%+d", p->trans);
        gfx_text(g_fonts.mo11, 668, y + 4, p->trans ? b : "0", f3 ? C_MINT2 : C_TEXT, 255, 0);
        if (f3) gfx_fill(668, y + ZONE_H - 3, 24, 1, C_MINT, 255);
    }

    /* state card: what is loaded, and the list */
    int cx = RIGHT_X, cy = 44, cw = 212, ch = SCR_H - 40 - 12 - cy;
    card(cx, cy, cw, ch);
    gfx_text(g_fonts.s11, cx + 12, cy + 12, "STATE", C_GREY, 255, 2);
    if (s_name_edit) {
        for (int i = 0; i < PERF_NAME; i++) {
            char c[2] = { s_name_buf[i], 0 };
            if (i == s_name_cur) gfx_round(cx + 10 + i * 15, cy + 28, 15, 22, 3, C_MINT, 255);
            gfx_text(g_fonts.m15, cx + 12 + i * 15, cy + 30, c, i == s_name_cur ? C_BG : C_TEXT, 255, 0);
        }
        gfx_text(g_fonts.mo10, cx + 12, cy + 54, "L R MOVE  U D LETTER  X KEEP  O DROP", C_AMBER, 255, 0);
    } else {
        gfx_text_fit(g_fonts.m15, cx + 12, cy + 30, cw - 24, s_perf_name, C_TEXT, 255);
        if (s_perf_msg_ms > 0) gfx_text(g_fonts.mo10, cx + 12, cy + 54, s_perf_msg, C_AMBER, 255, 0);
        else {   /* bank in use: what a save will store */
            char bl[40];
            snprintf(bl, sizeof bl, "%.30s", g_syn_bank_name);
            char *dot = strrchr(bl, '.'); if (dot) *dot = 0;
            int x = gfx_text(g_fonts.mo10, cx + 12, cy + 54, "BANK ", C_GREY, 255, 0);
            gfx_text_fit(g_fonts.mo10, x, cy + 54, cx + cw - 12 - x, bl, C_TEXT2, 255);
        }
    }
    gfx_fill(cx + 12, cy + 68, cw - 24, 1, C_LINE, 255);
    int nf = perf_factory_count(), total = PERF_LIST_N;
    int first = s_perf_sel - LIST_ROWS / 2;
    if (first > total - LIST_ROWS) first = total - LIST_ROWS;
    if (first < 0) first = 0;
    for (int r = 0; r < LIST_ROWS && first + r < total; r++) {
        int i = first + r, y = cy + LIST_Y - 44 + r * 18;
        int cur = i == s_perf_sel;
        if (cur) { gfx_round(cx + 6, y - 2, cw - 12, 18, 4, C_PANEL2, 255); gfx_fill(cx + 6, y + 1, 2, 12, C_MINT, 255); }
        const char *name; uint32_t col;
        if (i < nf) { snprintf(b, sizeof b, "F%d", i + 1); name = perf_factory(i)->name; col = C_TEXT2; }
        else { snprintf(b, sizeof b, "U%02d", i - nf + 1); name = perf_user_exists(i - nf) ? perf_user_name(i - nf) : "EMPTY"; col = perf_user_exists(i - nf) ? C_TEXT2 : C_GREY; }
        gfx_text(g_fonts.mo10, cx + 14, y + 3, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s12, cx + 46, y + 1, cw - 70, name, cur ? C_TEXT : col, 255);
        if (i == s_perf_cur) gfx_disc(cx + cw - 14, y + 7, 3, C_MINT, 255);
    }
    /* touch buttons */
    int by = cy + ch - 40;
    gfx_round(cx + 12, by, 88, 28, 6, C_PANEL2, 255);
    gfx_text_center(g_fonts.s11, cx + 56, by + 8, "LOAD", C_TEXT, 255, 2);
    gfx_round(cx + 112, by, 88, 28, 6, C_PANEL2, 255);
    gfx_text_center(g_fonts.s11, cx + 156, by + 8, "SAVE", C_TEXT, 255, 2);

    gfx_text(g_fonts.mo10, 28, SCR_H - 58, "PAD MOVES   O + PAD CHANGES   O LOADS   SQUARE SAVES THE STATE   SELECT NAME   X AUDITION", C_GREY, 255, 0);
}

/* ---- EFFECTS ------------------------------------------------------------------ */
static void fx_card(int x, int y, int h, const char *title)
{
    card(x, y, 300, h);
    gfx_text(g_fonts.s11, x + 16, y + 12, title, C_GREY, 255, 2);
}
static void fx_row(int k, const char *label, const char *val)
{
    int x, y, w;
    fx_slider_pos(k, &x, &y, &w);
    int sel = k == s_fx_sel;
    gfx_text(g_fonts.s11, x, y, label, sel ? C_TEXT : C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo14, x + w, y - 3, val, C_TEXT, 255, 0);
    slider(x, y + 18, w, fx_get(k), sel);
}
static void draw_fx(const UiStatus *st)
{
    (void)st;
    char b[24];
    fx_card(16, 58, 190, "REVERB");
    snprintf(b, sizeof b, "%d", g_fx.rev_size); fx_row(FXS_REV_SIZE, "SIZE", b);
    snprintf(b, sizeof b, "%d", g_fx.rev_damp); fx_row(FXS_REV_DAMP, "DAMPING", b);
    snprintf(b, sizeof b, "%d", g_fx.rev_level); fx_row(FXS_REV_LEVEL, "LEVEL", b);
    fx_card(16, 260, 190, "CHORUS");
    snprintf(b, sizeof b, "%.1f Hz", 0.1f + 4.9f * (g_fx.cho_rate / 127.0f)); fx_row(FXS_CHO_RATE, "RATE", b);
    snprintf(b, sizeof b, "%d", g_fx.cho_depth); fx_row(FXS_CHO_DEPTH, "DEPTH", b);
    snprintf(b, sizeof b, "%d", g_fx.cho_level); fx_row(FXS_CHO_LEVEL, "LEVEL", b);
    fx_card(330, 58, 190, "MASTER EQ");
    snprintf(b, sizeof b, "%+.1f dB", (g_fx.eq_lo - 64) * (12.0f / 63.0f)); fx_row(FXS_EQ_LO, "BASS  150 Hz", b);
    snprintf(b, sizeof b, "%+.1f dB", (g_fx.eq_mid - 64) * (12.0f / 63.0f)); fx_row(FXS_EQ_MID, "MID  1.2 kHz", b);
    snprintf(b, sizeof b, "%+.1f dB", (g_fx.eq_hi - 64) * (12.0f / 63.0f)); fx_row(FXS_EQ_HI, "TREBLE  6 kHz", b);
    fx_card(330, 260, 146, "OUTPUT");
    fx_row(FXS_LIMITER, "LIMITER  -1 dB", g_fx.limiter ? "ON" : "OFF");
    snprintf(b, sizeof b, "%d", g_syn_master); fx_row(FXS_MASTER, "MASTER", b);
    fx_card(644, 58, 146, "PART SENDS");
    snprintf(b, sizeof b, "%d", g_parts[s_part].rev); fx_row(FXS_PART_REV, "TO REVERB", b);
    snprintf(b, sizeof b, "%d", g_parts[s_part].cho); fx_row(FXS_PART_CHO, "TO CHORUS", b);
    {
        char t[48];
        snprintf(t, sizeof t, "PART %02d  %.20s", s_part + 1, g_parts[s_part].name[0] ? g_parts[s_part].name : "-");
        gfx_text_right(g_fonts.mo10, 644 + 300 - 16, 58 + 13, t, C_MINT, 255, 0);
    }
    card(644, 216, 300, 110);
    gfx_text(g_fonts.s11, 660, 228, "SIGNAL PATH", C_GREY, 255, 2);
    if (g_syn_mode == 1 && mt32_is_open()) {
        gfx_text(g_fonts.s12, 660, 250, "MT-32 mode: the emulator's output", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 266, "through the EQ and the limiter.", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 282, "Its reverb is on the MT-32 page;", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 298, "sends and chorus do not apply.", C_TEXT2, 255, 0);
    } else {
        gfx_text(g_fonts.s12, 660, 250, "Parts, each with its own sends,", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 266, "into the reverb and the chorus,", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 282, "then the EQ and the limiter.", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12, 660, 298, "CC 91 and 93 set the sends.", C_TEXT2, 255, 0);
    }
    gfx_text(g_fonts.mo10, 28, SCR_H - 58, "PAD MOVES   O + PAD CHANGES   TRIANGLE NEXT PART FOR THE SENDS   X AUDITION   TOUCH DRAGS", C_GREY, 255, 0);
}

/* load the entry under the cursor */
static void perf_after_apply(void)
{
    int want = perf_wanted_mode();
    if (mt32_is_open() && mt32_settings_stale()) {   /* emulation settings differ: reopen, then return */
        int was_mt32 = g_syn_mode == 1;
        if (was_mt32) { syn_event(SEV_MODE, 0, 0, 0); plat_sleep_ms(30); }
        mt32_open("ux0:data/prism/mt32");
        if (want < 0 && was_mt32 && mt32_is_open()) syn_event(SEV_MODE, 0, 1, 0);
    }
    if (want == 1 && mt32_is_open() && g_syn_mode != 1) syn_event(SEV_MODE, 0, 1, 0);
    if (want == 0 && g_syn_mode != 0) syn_event(SEV_MODE, 0, 0, 0);
    if (want == 1 && !mt32_is_open()) toast("STATE WANTS MT-32: NO ROMS");
}

static void perf_load_sel(void)
{
    int nf = perf_factory_count();
    Perf pf;
    if (s_perf_sel < nf) pf = *perf_factory(s_perf_sel);
    else if (perf_user_load(s_perf_sel - nf, &pf)) { perf_msg("EMPTY SLOT"); return; }
    int r = perf_apply(&pf);
    perf_after_apply();
    cfg_set_state(s_perf_sel);
    snprintf(s_perf_name, sizeof s_perf_name, "%s", pf.name[0] ? pf.name : "UNNAMED");
    s_perf_cur = s_perf_sel;
    if (r == 1) { char m[40]; snprintf(m, sizeof m, "BANK MISSING: %.20s", pf.bank); perf_msg(m); }
    else if (r < 0) perf_msg("ITS BANK WOULD NOT LOAD");
    else perf_msg("LOADED");
}
static void perf_save_sel(void)
{
    int nf = perf_factory_count();
    if (s_perf_sel < nf) { perf_msg("FACTORY: PICK A USER SLOT"); return; }
    Perf pf;
    perf_capture(&pf);
    int slot = s_perf_sel - nf;
    const char *nm = perf_user_exists(slot) ? perf_user_name(slot) : s_perf_name;
    if (!nm[0]) nm = "UNNAMED";
    snprintf(pf.name, sizeof pf.name, "%.12s", nm);
    if (perf_user_save(slot, &pf)) { perf_msg("SAVE FAILED"); return; }
    snprintf(s_perf_name, sizeof s_perf_name, "%s", pf.name);
    s_perf_cur = s_perf_sel;
    cfg_set_state(s_perf_sel);
    perf_msg("SAVED");
}

/* ---- MT-32 -------------------------------------------------------------------- */
static void draw_mt32(const UiStatus *st)
{
    char b[96];
    int open = mt32_is_open();
    /* display */
    gfx_round(16, 58, SCR_W - 32, 96, 12, C_DEEP, 255);
    gfx_frame(16, 58, SCR_W - 32, 96, 12, C_LINE, 255);
    gfx_text(g_fonts.s11, 44, 70, "DISPLAY", C_GREY, 255, 2);
    gfx_text_right(g_fonts.mo10, SCR_W - 44, 71, "20 x 1", C_GREY, 255, 0);
    gfx_disc(SCR_W - 60, 122, 4, open && g_mt32.led ? C_AMBER : C_LINE, 255);
    gfx_text_right(g_fonts.mo10, SCR_W - 70, 118, "MIDI MESSAGE", C_GREY, 255, 0);
    if (open) gfx_text(g_fonts.mo44, 44, 92, g_mt32.lcd[0] ? g_mt32.lcd : "", C_MINT, 255, 6);
    else gfx_text(g_fonts.mo44, 44, 92, g_syn_mode == 1 ? "" : "NO ROMS LOADED", C_GREY, 255, 6);
    /* parts */
    for (int p = 0; p < 9; p++) {
        int x = 16 + p * 103, y = 166, w = 96;
        int on = open && (g_mt32.part_mask >> p) & 1;
        gfx_round(x, y, w, 62, 8, on ? C_PANEL2 : C_PANEL, 255);
        gfx_frame(x, y, w, 62, 8, on ? C_MINT : C_LINE, 255);
        snprintf(b, sizeof b, p < 8 ? "PART %d" : "RHYTHM", p + 1);
        gfx_text(g_fonts.mo10, x + 10, y + 10, b, C_GREY, 255, 0);
        if (open && g_mt32.chan[p] < 16) snprintf(b, sizeof b, "CH %d", g_mt32.chan[p] + 1);
        else snprintf(b, sizeof b, open ? "OFF" : "CH -");
        gfx_text_right(g_fonts.mo10, x + w - 10, y + 10, b, C_GREY, 255, 0);
        gfx_text_fit(g_fonts.s12, x + 10, y + 28, w - 20, open && g_mt32.patch[p][0] ? g_mt32.patch[p] : "-", on ? C_TEXT : C_TEXT2, 255);
        gfx_fill(x + 10, y + 50, w - 20, 3, on ? C_MINT : C_LINE, 255);
    }
    /* settings and cards */
    int cx = 16, cy = 244, cw = 300;
    card(cx, cy, cw, 200);
    gfx_text(g_fonts.s11, cx + 16, cy + 12, "MODULE", C_GREY, 255, 2);
    static const char *labels[MR_MODULE_ROWS] = { "MODE", "REVERB", "OUTPUT GAIN", "CHANNEL MAP", "ROM FOLDER" };
    for (int r = 0; r < MR_MODULE_ROWS; r++) {
        int y = cy + 36 + r * 30;
        int sel = r == s_mt_row;
        if (sel) { gfx_round(cx + 8, y - 4, cw - 16, 26, 5, C_PANEL2, 255); gfx_fill(cx + 8, y, 2, 18, C_MINT, 255); }
        gfx_text(g_fonts.s11, cx + 16, y + 3, labels[r], sel ? C_TEXT : C_GREY, 255, 2);
        switch (r) {
        case MR_MODE:
            gfx_text_right(g_fonts.mo14, cx + cw - 16, y, g_syn_mode == 1 ? "MT-32" : "GM", g_syn_mode == 1 ? C_MINT : C_TEXT, 255, 0);
            break;
        case MR_REVERB:
            gfx_text_right(g_fonts.mo14, cx + cw - 16, y, g_mt32.reverb_on ? "ON" : "OFF", g_mt32.reverb_on ? C_MINT : C_TEXT, 255, 0);
            break;
        case MR_GAIN:
            snprintf(b, sizeof b, "%d", g_mt32.gain);
            gfx_text_right(g_fonts.mo14, cx + cw - 16, y, b, C_TEXT, 255, 0);
            break;
        case MR_MAP:
            gfx_text_right(g_fonts.mo14, cx + cw - 16, y, g_mt32.map_stock ? "MT-32  2-9" : "KEYBOARD  1-8", C_TEXT, 255, 0);
            break;
        default:
            gfx_text_right(g_fonts.mo14, cx + cw - 16, y, "X REOPEN", C_TEXT, 255, 0);
            break;
        }
    }
    /* emulation settings, each applied at the next open */
    int ex = cx + cw + 12, ew = 300;
    card(ex, cy, ew, 142);
    gfx_text(g_fonts.s11, ex + 16, cy + 12, "EMULATION  -  AT THE NEXT REOPEN", C_GREY, 255, 2);
    static const char *elabels[5] = { "ANALOG STAGE", "RATE CONVERTER", "GENERATOR", "PARTIALS", "CORES" };
    static const char *SRC_NAME[4] = { "LINEAR", "FAST", "GOOD", "BEST" };
    for (int r = 0; r < 5; r++) {
        int y = cy + 34 + r * 21, row = MR_ANALOG + r;
        int sel = row == s_mt_row;
        if (sel) { gfx_round(ex + 8, y - 3, ew - 16, 21, 5, C_PANEL2, 255); gfx_fill(ex + 8, y, 2, 15, C_MINT, 255); }
        gfx_text(g_fonts.s11, ex + 16, y + 1, elabels[r], sel ? C_TEXT : C_GREY, 255, 2);
        const char *v;
        switch (row) {
        case MR_ANALOG: v = ANALOG_NAME[g_mt32.analog & 3]; break;
        case MR_SRC: v = SRC_NAME[g_mt32.srcq & 3]; break;
        case MR_RENDER: v = g_mt32.renderer ? "FLOAT" : "INT 16  EXACT"; break;
        case MR_CORES: snprintf(b, sizeof b, "%d OF 3  (%d RUN)", g_mt32.cores ? g_mt32.cores : 1, g_mt32.cores_used ? g_mt32.cores_used : 1); v = b; break;
        default: snprintf(b, sizeof b, "%d OF 32", g_mt32.partials_cap ? g_mt32.partials_cap : 32); v = b; break;
        }
        gfx_text_right(g_fonts.mo11, ex + ew - 16, y, v, C_TEXT, 255, 0);
    }
    card(ex, cy + 150, ew, 92);
    gfx_text(g_fonts.s11, ex + 16, cy + 162, "ROMS", C_GREY, 255, 2);
    gfx_text(g_fonts.s12, ex + 16, cy + 178, "Control", C_TEXT, 255, 0);
    gfx_text_fit(g_fonts.mo10, ex + 76, cy + 180, ew - 92, open ? g_mt32.ctrl_desc : "-", C_GREY, 255);
    gfx_text_fit(g_fonts.mo10, ex + 76, cy + 194, ew - 92, open ? g_mt32.ctrl_file : "", C_TEXT2, 255);
    gfx_text(g_fonts.s12, ex + 16, cy + 208, "PCM", C_TEXT, 255, 0);
    gfx_text_fit(g_fonts.mo10, ex + 76, cy + 210, ew - 92, open ? g_mt32.pcm_desc : "-", C_GREY, 255);
    gfx_text_fit(g_fonts.mo10, ex + 76, cy + 224, ew - 92, open ? g_mt32.pcm_file : "ux0:data/prism/mt32", C_TEXT2, 255);
    int sx = ex + ew + 12, sw = SCR_W - 16 - sx;
    card(sx, cy, sw, 200);
    gfx_text(g_fonts.s11, sx + 16, cy + 12, "STATUS", C_GREY, 255, 2);
    if (open) {
        gfx_text(g_fonts.s12, sx + 16, cy + 32, g_syn_mode == 1 ? "MT-32 ENGINE ACTIVE" : "MT-32 ENGINE STANDBY", g_syn_mode == 1 ? C_MINT : C_TEXT2, 255, 0);
        /* the three cores as meters: the audio core with the worst block marked, then the helpers */
        static const char *CL[3] = { "CPU 1  AUDIO", "CPU 2  HELPER", "CPU 0  HELPER + PANEL" };
        int bx = sx + 16, bw = sw - 32;
        for (int i = 0; i < 3; i++) {
            int y = cy + 50 + i * 26;
            int v = st->core_load[i] < 0 ? 0 : st->core_load[i] > 100 ? 100 : st->core_load[i];
            gfx_text(g_fonts.mo10, bx, y, CL[i], C_GREY, 255, 0);
            if (i == 0) {
                int wv = st->load < 0 ? 0 : st->load > 100 ? 100 : st->load;
                snprintf(b, sizeof b, "%d%%  WORST %d%%", v, st->load);
                gfx_text_right(g_fonts.mo10, sx + sw - 16, y, b, st->load >= 100 ? C_AMBER : C_TEXT, 255, 0);
                gfx_fill(bx, y + 13, bw, 6, C_PANEL2, 255);
                gfx_fill(bx, y + 13, bw * v / 100, 6, v >= 85 ? C_AMBER : C_MINT, 255);
                gfx_fill(bx + bw * wv / 100 - 1, y + 11, 2, 10, C_TEXT, 255);
            } else {
                snprintf(b, sizeof b, "%d%%", v);
                gfx_text_right(g_fonts.mo10, sx + sw - 16, y, b, C_TEXT, 255, 0);
                gfx_fill(bx, y + 13, bw, 6, C_PANEL2, 255);
                gfx_fill(bx, y + 13, bw * v / 100, 6, v >= 85 ? C_AMBER : C_MINT, 255);
            }
        }
        {   /* partials against the cap */
            int y = cy + 128, pm = g_mt32.partials_max ? g_mt32.partials_max : 32;
            int pv = g_mt32.partials > pm ? pm : g_mt32.partials;
            gfx_text(g_fonts.mo10, bx, y, "PARTIALS", C_GREY, 255, 0);
            snprintf(b, sizeof b, "%d / %d", g_mt32.partials, pm);
            gfx_text_right(g_fonts.mo10, sx + sw - 16, y, b, C_TEXT, 255, 0);
            gfx_fill(bx, y + 13, bw, 6, C_PANEL2, 255);
            gfx_fill(bx, y + 13, bw * pv / pm, 6, pv >= pm ? C_AMBER : C_MINT, 255);
        }
        snprintf(b, sizeof b, "BENCH %%  1C %d  2C %d  3C %d  STEAL %d", g_mt32.bench[0], g_mt32.bench[1], g_mt32.bench[2], g_mt32.bench[3]);
        gfx_text_fit(g_fonts.mo10, bx, cy + 154, bw, b, C_GREY, 255);
        {
            int ran = (g_mt32.selfcheck & 0x100) != 0, ok = (g_mt32.selfcheck & 3) == 3;
            snprintf(b, sizeof b, "CHANNELS %s   SELF-CHECK %s", g_mt32.map_stock ? "2-9 + 10" : "1-8 + 10", !ran ? "-" : ok ? "PASS" : "FAIL");
            gfx_text_fit(g_fonts.mo10, bx, cy + 168, bw, b, ran && !ok ? C_AMBER : C_GREY, 255);
        }
        if (st->load >= 100) gfx_text_fit(g_fonts.mo10, bx, cy + 182, bw, "DROPOUTS: RAISE AUDIO AHEAD ON SETUP", C_AMBER, 255);
    } else {
        gfx_text(g_fonts.s12, sx + 16, cy + 32, "NO ROMS LOADED", C_AMBER, 255, 0);
        gfx_text_fit(g_fonts.mo10, sx + 16, cy + 56, sw - 32, "ux0:data/prism/mt32", C_TEXT2, 255);
        gfx_text_fit(g_fonts.mo10, sx + 16, cy + 72, sw - 32, "MT32_CONTROL.ROM + MT32_PCM.ROM", C_GREY, 255);
        gfx_text_fit(g_fonts.mo10, sx + 16, cy + 86, sw - 32, "CM32L_CONTROL.ROM + CM32L_PCM.ROM", C_GREY, 255);
        gfx_text_fit(g_fonts.mo10, sx + 16, cy + 100, sw - 32, "SPLIT DUMPS ARE PAIRED BY CONTENT", C_GREY, 255);
        gfx_text_fit(g_fonts.s12, sx + 16, cy + 122, sw - 32, g_mt32.status[0] ? g_mt32.status : "Nothing loaded.", C_AMBER, 255);
        snprintf(b, sizeof b, "%d file%s seen", g_mt32.roms_found, g_mt32.roms_found == 1 ? "" : "s");
        gfx_text(g_fonts.mo10, sx + 16, cy + 146, b, C_GREY, 255, 0);
    }
    gfx_text(g_fonts.mo10, 28, SCR_H - 58, "PAD MOVES   O + PAD CHANGES   X ACTS ON THE ROW   ROM FOLDER ROW: X REOPENS   START ALL OFF", C_GREY, 255, 0);
}

static void draw_soon(const char *what)
{
    card(SCR_W / 2 - 220, 200, 440, 120);
    gfx_text_center(g_fonts.b17, SCR_W / 2, 226, what, C_TEXT, 255, 2);
    gfx_text_center(g_fonts.s12, SCR_W / 2, 262, "Not in this build yet.", C_GREY, 255, 0);
}

/* ---- the boot screen ---------------------------------------------------------- */
void ui_boot_frame(const char *headline)
{
    gfx_target(plat_fb_begin());
    gfx_clear(C_BG);
    gfx_text(g_fonts.b34 ? g_fonts.b34 : g_fonts.b17, 60, 120, "PRISM", C_TEXT, 255, 10);
    gfx_text(g_fonts.s13 ? g_fonts.s13 : g_fonts.s11, 60, 170, headline, C_GREY, 255, 0);
    int n = plat_log_count();
    for (int i = 0; i < n; i++) gfx_text(g_fonts.mo12 ? g_fonts.mo12 : g_fonts.mo11, 60, 210 + i * 16, plat_log_line(i), C_TEXT2, 255, 0);
    gfx_fill(60, 116, 3, 30, C_MINT, 255);
    plat_fb_flip();
}

/* Boot question: reload the last state or start with the default GM set. X or a
 * tap on the left answers yes; O or a tap on the right, no. */
int ui_boot_ask(const char *state_name)
{
    unsigned int prev = 0xFFFFFFFFu;
    int touch_prev = 1;
    for (;;) {
        gfx_target(plat_fb_begin());
        gfx_clear(C_BG);
        gfx_text(g_fonts.b34 ? g_fonts.b34 : g_fonts.b17, 60, 120, "PRISM", C_TEXT, 255, 10);
        gfx_fill(60, 116, 3, 30, C_MINT, 255);
        gfx_text(g_fonts.s13 ? g_fonts.s13 : g_fonts.s11, 60, 170, "Reload the last state?", C_TEXT, 255, 0);
        char b[64];
        snprintf(b, sizeof b, "%.40s", state_name && state_name[0] ? state_name : "UNNAMED");
        gfx_round(60, 210, 400, 64, 8, C_PANEL2, 255);
        gfx_frame(60, 210, 400, 64, 8, C_MINT, 255);
        gfx_text(g_fonts.mo11 ? g_fonts.mo11 : g_fonts.mo10, 80, 222, "X   YES, RELOAD", C_MINT, 255, 0);
        gfx_text_fit(g_fonts.m15 ? g_fonts.m15 : g_fonts.s13, 80, 244, 360, b, C_TEXT, 255);
        gfx_round(500, 210, 400, 64, 8, C_PANEL2, 255);
        gfx_frame(500, 210, 400, 64, 8, C_LINE, 255);
        gfx_text(g_fonts.mo11 ? g_fonts.mo11 : g_fonts.mo10, 520, 222, "O   NO, THE DEFAULT GM SET", C_TEXT2, 255, 0);
        gfx_text(g_fonts.s12 ? g_fonts.s12 : g_fonts.s11, 520, 244, "GeneralUser GS, every part fresh", C_GREY, 255, 0);
        int n = plat_log_count();
        for (int i = 0; i < n && i < 14; i++) gfx_text(g_fonts.mo10 ? g_fonts.mo10 : g_fonts.mo11, 60, 300 + i * 14, plat_log_line(i), C_GREY, 255, 0);
        plat_fb_flip();
        PlatPad pad;
        plat_input_read(&pad);
        unsigned int dn = prev == 0xFFFFFFFFu ? 0 : (pad.buttons & ~prev);
        prev = pad.buttons;
        if (dn & SCE_CTRL_CROSS) return 1;
        if (dn & SCE_CTRL_CIRCLE) return 0;
        if (pad.touch && !touch_prev && pad.ty >= 200 && pad.ty < 290) {
            if (pad.tx >= 60 && pad.tx < 460) return 1;
            if (pad.tx >= 500 && pad.tx < 900) return 0;
        }
        touch_prev = pad.touch;
    }
}

void ui_set_perf(int idx, const char *name)
{
    s_perf_cur = s_perf_sel = idx;
    snprintf(s_perf_name, sizeof s_perf_name, "%s", name && name[0] ? name : "UNNAMED");
}

void ui_boot_progress(const char *headline, int pct, const char *detail)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    gfx_target(plat_fb_begin());
    gfx_clear(C_BG);
    gfx_text(g_fonts.b34 ? g_fonts.b34 : g_fonts.b17, 60, 120, "PRISM", C_TEXT, 255, 10);
    gfx_text(g_fonts.s13 ? g_fonts.s13 : g_fonts.s11, 60, 170, headline, C_GREY, 255, 0);
    /* progress bar between the headline and the log */
    gfx_round(60, 194, 400, 8, 4, C_PANEL2, 255);
    if (pct > 0) gfx_round(60, 194, 400 * pct / 100 < 8 ? 8 : 400 * pct / 100, 8, 4, C_MINT, 255);
    char b[48];
    snprintf(b, sizeof b, "%d%%", pct);
    gfx_text(g_fonts.mo11 ? g_fonts.mo11 : g_fonts.mo10, 472, 190, b, C_TEXT, 255, 0);
    if (detail) gfx_text(g_fonts.mo10 ? g_fonts.mo10 : g_fonts.mo11, 520, 191, detail, C_GREY, 255, 0);
    int n = plat_log_count();
    for (int i = 0; i < n; i++) gfx_text(g_fonts.mo12 ? g_fonts.mo12 : g_fonts.mo11, 60, 216 + i * 16, plat_log_line(i), C_TEXT2, 255, 0);
    gfx_fill(60, 116, 3, 30, C_MINT, 255);
    plat_fb_flip();
}

/* ---- input ------------------------------------------------------------------------ */
static void play_input_mt32(const PlatPad *pad, unsigned int dn, unsigned int nav, unsigned int b, int edit, int tdown)
{
    static int touch_knob = -1;
    if (s_part > 8) s_part = 0;
    if (s_browse) {   /* patch list has the pad */
        static int br_ms = 0, br_stick = 0;
        int ud = (b & SCE_CTRL_DOWN) ? 1 : (b & SCE_CTRL_UP) ? -1 : 0;
        if (ud) { if (dn & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { mt_browse_move(ud); br_ms = 0; } else if (++br_ms > 16 && (br_ms & 1)) mt_browse_move(ud); }
        else br_ms = 0;
        int sy = pad->ry < 90 ? -1 : pad->ry > 166 ? 1 : 0;
        if (sy) { if (++br_stick == 1 || br_stick > 14) { mt_browse_move(sy); if (br_stick > 14) br_stick = 10; } } else br_stick = 0;
        if (dn & SCE_CTRL_CIRCLE) s_browse = 0;
        if (dn & SCE_CTRL_CROSS) { s_aud_note = s_part == 8 ? 36 : 60; syn_event(SEV_NOTE_ON, s_part, s_aud_note, 100); }
        if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
        if (pad->touch) {
            static int drag_y = -1, drag_cur = 0;
            int h = BR_ROWS * BR_ROW_H + 52;
            if (tdown) {
                if (pad->tx < BR_X || pad->tx >= BR_X + BR_W || pad->ty < BR_Y || pad->ty >= BR_Y + h) { s_browse = 0; drag_y = -1; }
                else { drag_y = pad->ty; drag_cur = s_browse_cur; }
            } else if (drag_y >= 0) {
                int nc = drag_cur + (drag_y - pad->ty) / BR_ROW_H;
                if (nc < 0) nc = 0;
                if (nc > 127) nc = 127;
                if (nc != s_browse_cur) { s_browse_cur = nc; mt_set(s_part, MP_PATCH, nc); }
            }
        }
        return;
    }
    if ((dn & SCE_CTRL_TRIANGLE) && s_part < 8) { s_browse_cur = g_mt32.prog[s_part]; s_browse = 1; return; }
    if (s_focus == 0) {
        if (nav & SCE_CTRL_UP) s_part = (s_part + 8) % 9;
        if (nav & SCE_CTRL_DOWN) s_part = (s_part + 1) % 9;
        if (nav & SCE_CTRL_RIGHT) s_focus = 1;
        if (edit) mt_set(s_part, MP_PATCH, g_mt32.prog[s_part] + edit);
    } else {
        if (s_row >= MP_COUNT) s_row = 0;
        if (nav & SCE_CTRL_UP) s_row = (s_row + MP_COUNT - 1) % MP_COUNT;
        if (nav & SCE_CTRL_DOWN) s_row = (s_row + 1) % MP_COUNT;
        if (nav & SCE_CTRL_LEFT) s_focus = 0;
        if (edit) mt_set(s_part, s_row, mt_get(s_part, s_row) + edit * (s_row >= MP_VOL ? 2 : 1));
    }
    if (dn & SCE_CTRL_CROSS) { s_aud_note = s_part == 8 ? 36 : 60; syn_event(SEV_NOTE_ON, s_part, s_aud_note, 100); }
    if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
    /* touch: part row, name row, or a setting */
    if (pad->touch) {
        int cy0 = RAIL_Y - 14, ly = cy0 + 72, vx = CARD_X + 124, sw = CARD_W - 124 - 54;
        if (tdown) {
            touch_knob = -1;
            if (pad->tx >= RAIL_X && pad->tx < RAIL_X + RAIL_W && pad->ty >= RAIL_Y && pad->ty < RAIL_Y + 9 * ROW_H) {
                s_part = (pad->ty - RAIL_Y) / ROW_H; s_focus = 0;
            } else if (pad->tx >= CARD_X - 8 && pad->tx < CARD_X + CARD_W + 8 && pad->ty >= cy0 + 20 && pad->ty < cy0 + 52 && s_part < 8) {
                if (pad->tx >= CARD_X + CARD_W - 62 && pad->tx < CARD_X + CARD_W - 32) mt_set(s_part, MP_PATCH, g_mt32.prog[s_part] - 1);
                else if (pad->tx >= CARD_X + CARD_W - 30) mt_set(s_part, MP_PATCH, g_mt32.prog[s_part] + 1);
                else { s_browse_cur = g_mt32.prog[s_part]; s_browse = 1; }
            } else if (pad->tx >= CARD_X - 8 && pad->tx < CARD_X + CARD_W + 8 && pad->ty >= ly - 3 && pad->ty < ly + MP_COUNT * 22) {
                int r = (pad->ty - (ly - 3)) / 22;
                if (r >= 0 && r < MP_COUNT) {
                    s_focus = 1; s_row = r;
                    if (r == MP_PATCH && pad->tx >= vx && s_part < 8) { s_browse_cur = g_mt32.prog[s_part]; s_browse = 1; }
                    else if (r == MP_CH && pad->tx >= vx) mt_set(s_part, MP_CH, (g_mt32.chan[s_part] + 1) % 17);
                    else if (r > MP_CH && pad->tx >= vx - 8) touch_knob = r;
                }
            }
        }
        if (touch_knob > MP_CH) {
            int v = (pad->tx - vx) * 127 / (sw - 8);
            if (v != mt_get(s_part, touch_knob)) mt_set(s_part, touch_knob, v);
        }
    } else touch_knob = -1;
}

static void handle_input(const PlatPad *pad, const UiStatus *st)
{
    static unsigned int prev = 0;
    static int rep_ms = 0;
    static int touch_prev = 0, touch_knob = -1, touch_strip = -1;
    unsigned int b = pad->buttons, dn = b & ~prev, up = prev & ~b;
    prev = b;
    (void)st;

    if (dn & SCE_CTRL_LTRIGGER) s_page = (s_page + PG_COUNT - 1) % PG_COUNT;
    if (dn & SCE_CTRL_RTRIGGER) s_page = (s_page + 1) % PG_COUNT;
    if (dn & SCE_CTRL_START) syn_event(SEV_ALLOFF, 0, 0, 0);
    {   /* hold SELECT for a second on any page: reconnect USB */
        static int sel_frames = 0;
        if ((b & SCE_CTRL_SELECT) && !s_name_edit) { if (++sel_frames == 60) { plat_midi_reconnect(); toast("RECONNECTING USB"); } }
        else sel_frames = 0;
    }

    /* left/right auto-repeat */
    int lr = (b & SCE_CTRL_RIGHT) ? 1 : (b & SCE_CTRL_LEFT) ? -1 : 0;
    int step = 0;
    if (lr) { if (dn & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) { step = lr; rep_ms = 0; } else if (++rep_ms > 18 && (rep_ms & 1)) step = lr; }
    else rep_ms = 0;

    /* the model on every page: the pad moves; the pad with O held changes the
     * value under the cursor; O released without a change is a tap */
    int o_held = (b & SCE_CTRL_CIRCLE) != 0;
    static int ud_ms = 0, o_used = 0;
    int ud = (b & SCE_CTRL_UP) ? 1 : (b & SCE_CTRL_DOWN) ? -1 : 0, ustep = 0;
    if (ud) { if (dn & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { ustep = ud; ud_ms = 0; } else if (++ud_ms > 18 && (ud_ms & 1)) ustep = ud; }
    else ud_ms = 0;
    int edit = o_held ? (step ? step : ustep) : 0;
    unsigned int nav = o_held ? 0 : dn;
    if (edit) o_used = 1;
    int o_tap = (up & SCE_CTRL_CIRCLE) && !o_used;
    if (!o_held) o_used = 0;
    (void)o_tap;

    int tdown = pad->touch && !touch_prev;
    touch_prev = pad->touch;
    if (tdown && pad->ty >= SCR_H - 40) { int t = tab_hit(pad->tx); if (t >= 0) s_page = t; }
    /* tap on the MIDI lamp: reconnect USB */
    if (tdown && pad->ty < 44 && pad->tx >= s_lamp_x - 40 && pad->tx < s_lamp_x + 44) { plat_midi_reconnect(); toast("RECONNECTING USB"); }

    switch (s_page) {
    case PG_PLAY:
        if (g_syn_mode == 1 && mt32_is_open()) { play_input_mt32(pad, dn, nav, b, edit, tdown); if (dn & SCE_CTRL_CIRCLE) o_used = 1; break; }
        if (s_browse) {   /* patch browser has the pad */
            static int br_ms = 0, br_stick = 0;
            int ud = (b & SCE_CTRL_DOWN) ? 1 : (b & SCE_CTRL_UP) ? -1 : 0;
            if (ud) { if (dn & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { browse_move(ud); br_ms = 0; } else if (++br_ms > 16 && (br_ms & 1)) browse_move(ud); }
            else br_ms = 0;
            int sy = pad->ry < 90 ? -1 : pad->ry > 166 ? 1 : 0;
            if (sy) { if (++br_stick == 1 || br_stick > 14) { browse_move(sy); if (br_stick > 14) br_stick = 10; } } else br_stick = 0;
            if (dn & SCE_CTRL_CIRCLE) { s_browse = 0; o_used = 1; }
            if (dn & SCE_CTRL_CROSS) { s_aud_note = s_part == 9 ? 36 : 60; syn_event(SEV_NOTE_ON, s_part, s_aud_note, 100); }
            if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
            if (pad->touch) {
                static int drag_y = -1, drag_cur = 0;
                int h = BR_ROWS * BR_ROW_H + 52;
                if (tdown) {
                    if (pad->tx < BR_X || pad->tx >= BR_X + BR_W || pad->ty < BR_Y || pad->ty >= BR_Y + h) { s_browse = 0; drag_y = -1; }
                    else { drag_y = pad->ty; drag_cur = s_browse_cur; }
                } else if (drag_y >= 0) {
                    int d = (drag_y - pad->ty) / BR_ROW_H;
                    if (d && s_bn) { int nc = drag_cur + d; if (nc < 0) nc = 0; if (nc >= s_bn) nc = s_bn - 1; if (nc != s_browse_cur) { s_browse_cur = nc; browse_apply(); } }
                }
            }
            break;
        }
        if (dn & SCE_CTRL_TRIANGLE) { browse_open(); break; }
        if (s_focus == 0) {   /* the rail; RIGHT crosses to the settings */
            if (nav & SCE_CTRL_UP) s_part = (s_part + SYN_PARTS - 1) % SYN_PARTS;
            if (nav & SCE_CTRL_DOWN) s_part = (s_part + 1) % SYN_PARTS;
            if (nav & SCE_CTRL_RIGHT) s_focus = 1;
            if (edit) ps_set(PS_PATCH, g_parts[s_part].prog + edit);
        } else {              /* the settings; LEFT goes back to the rail */
            if (nav & SCE_CTRL_UP) s_row = (s_row + PS_COUNT - 1) % PS_COUNT;
            if (nav & SCE_CTRL_DOWN) s_row = (s_row + 1) % PS_COUNT;
            if (nav & SCE_CTRL_LEFT) s_focus = 0;
            if (edit) ps_set(s_row, ps_get(&g_parts[s_part], s_row) + edit * ((s_row >= PS_LEVEL && s_row <= PS_CHO) ? 2 : 1));
        }
        if (dn & SCE_CTRL_SQUARE) syn_event(SEV_MUTE, s_part, !g_parts[s_part].mute, 0);
        if (dn & SCE_CTRL_CROSS) { s_aud_note = s_part == 9 ? 36 : 60; syn_event(SEV_NOTE_ON, s_part, s_aud_note, 100); }
        if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
        /* touch: part row, name row, or a setting slider */
        if (pad->touch) {
            int cy0 = RAIL_Y - 14, ly = cy0 + 72, vx = CARD_X + 124, sw = CARD_W - 124 - 54;
            if (tdown) {
                touch_knob = -1;
                if (pad->tx >= RAIL_X && pad->tx < RAIL_X + RAIL_W && pad->ty >= RAIL_Y && pad->ty < RAIL_Y + SYN_PARTS * ROW_H) {
                    s_part = (pad->ty - RAIL_Y) / ROW_H; s_focus = 0;
                } else if (pad->tx >= CARD_X - 8 && pad->tx < CARD_X + CARD_W + 8 && pad->ty >= cy0 + 20 && pad->ty < cy0 + 52) {
                    if (pad->tx >= CARD_X + CARD_W - 62 && pad->tx < CARD_X + CARD_W - 32) ps_set(PS_PATCH, g_parts[s_part].prog - 1);
                    else if (pad->tx >= CARD_X + CARD_W - 30) ps_set(PS_PATCH, g_parts[s_part].prog + 1);
                    else browse_open();
                } else if (pad->tx >= CARD_X - 8 && pad->tx < CARD_X + CARD_W + 8 && pad->ty >= ly - 3 && pad->ty < ly + PS_COUNT * 22) {
                    int r = (pad->ty - (ly - 3)) / 22;
                    if (r >= 0 && r < PS_COUNT) {
                        s_focus = 1; s_row = r;
                        if (r == PS_PATCH && pad->tx >= vx) browse_open();
                        else if (r == PS_CH && pad->tx >= vx) ps_set(PS_CH, (g_parts[s_part].rx + 1) % (SYN_RX_OFF + 1));
                        else if (r > PS_CH && pad->tx >= vx - 8) touch_knob = r;
                    }
                }
            }
            if (touch_knob > PS_CH) {
                int v = (pad->tx - vx) * ps_max(touch_knob) / (sw - 8);
                if (v != ps_get(&g_parts[s_part], touch_knob)) ps_set(touch_knob, v);
            }
        } else touch_knob = -1;
        break;
    case PG_MIX:
        if (nav & SCE_CTRL_LEFT) s_part = s_part == 0 ? SYN_PARTS : s_part - 1;
        if (nav & SCE_CTRL_RIGHT) s_part = (s_part + 1) % (SYN_PARTS + 1);
        if (edit) {   /* O with the pad: the strip's level */
            if (s_part == SYN_PARTS) { int v = g_syn_master + edit * 2; if (v < 0) v = 0; if (v > 127) v = 127; syn_event(SEV_MASTER, 0, v, 0); }
            else { int v = g_parts[s_part].level + edit * 2; if (v < 0) v = 0; if (v > 127) v = 127; syn_event(SEV_LEVEL, s_part, v, 0); }
        }
        if ((dn & SCE_CTRL_SQUARE) && s_part < SYN_PARTS) syn_event(SEV_MUTE, s_part, !g_parts[s_part].mute, 0);
        if (pad->touch) {
            if (tdown) {
                touch_strip = -1;
                int i = (pad->tx - MIX_X) / (STRIP_W + 2);
                if (pad->tx >= MIX_X && i >= 0 && i < SYN_PARTS) {
                    int x = MIX_X + i * (STRIP_W + 2), fy = MIX_Y + 52;
                    if (pad->ty >= fy + FADER_H + 20 && pad->ty < fy + FADER_H + 48) syn_event(SEV_MUTE, i, !g_parts[i].mute, 0);
                    else { s_part = i; if (pad->ty >= fy - 6 && pad->ty < fy + FADER_H + 6 && pad->tx >= x + 16) touch_strip = i; }
                }
                if (pad->tx >= SCR_W - 16 - 96 && pad->ty >= MIX_Y + 30 && pad->ty < MIX_Y + 30 + FADER_H + 22) { touch_strip = SYN_PARTS; s_part = SYN_PARTS; }
            }
            if (touch_strip >= 0) {
                int fy = touch_strip == SYN_PARTS ? MIX_Y + 30 : MIX_Y + 52, fh = touch_strip == SYN_PARTS ? FADER_H + 22 : FADER_H;
                int v = (fy + fh - pad->ty) * 127 / fh; if (v < 0) v = 0; if (v > 127) v = 127;
                if (touch_strip == SYN_PARTS) { if (v != g_syn_master) syn_event(SEV_MASTER, 0, v, 0); }
                else if (v != g_parts[touch_strip].level) syn_event(SEV_LEVEL, touch_strip, v, 0);
            }
        } else touch_strip = -1;
        break;
    case PG_SETUP: {
        int nb = s_sf_count + 1, items = setup_items();
        if (s_setup_list >= nb) s_setup_list = 0;
        /* two columns: the list, and the module panel */
        if ((nav & SCE_CTRL_RIGHT) && s_setup_cur < nb) { s_setup_list = s_setup_cur; s_setup_cur = nb; }
        else if ((nav & SCE_CTRL_LEFT) && s_setup_cur >= nb) s_setup_cur = s_setup_list;
        if (nav & SCE_CTRL_UP) s_setup_cur = s_setup_cur >= nb ? (s_setup_cur == nb ? items - 1 : s_setup_cur - 1) : (s_setup_cur == 0 ? nb - 1 : s_setup_cur - 1);
        if (nav & SCE_CTRL_DOWN) s_setup_cur = s_setup_cur >= nb ? (s_setup_cur == items - 1 ? nb : s_setup_cur + 1) : (s_setup_cur == nb - 1 ? 0 : s_setup_cur + 1);
        if (s_setup_cur < nb) s_setup_list = s_setup_cur;
        if (edit && s_setup_cur == nb) { int v = g_syn_master + edit * 2; if (v < 0) v = 0; if (v > 127) v = 127; syn_event(SEV_MASTER, 0, v, 0); }
        if (edit && s_setup_cur == nb + 1) { int v = g_syn_poly + edit * 8; if (v < 8) v = 8; if (v > 127) v = 127; syn_event(SEV_POLY, 0, v, 0); }
        if (edit && s_setup_cur == nb + 2) { plat_audio_set_ahead(plat_audio_ahead() + edit); cfg_set_ahead(plat_audio_ahead()); }
        int load_it = (dn & SCE_CTRL_CROSS) && s_setup_cur < nb;
        {
            static int touch_su = -1;
            if (pad->touch) {
                int first = (s_setup_cur < nb ? s_setup_cur : 0) - SU_ROWS / 2;
                if (first > nb - SU_ROWS) first = nb - SU_ROWS;
                if (first < 0) first = 0;
                int mx = SU_LIST_X + SU_LIST_W + 16, mw = SCR_W - 16 - mx;
                if (tdown) {
                    touch_su = -1;
                    if (pad->tx >= SU_LIST_X && pad->tx < SU_LIST_X + SU_LIST_W && pad->ty >= SU_LIST_Y + 20 && pad->ty < SU_LIST_Y + 22 + SU_ROWS * SU_ROW_H) {
                        int i = first + (pad->ty - (SU_LIST_Y + 20)) / SU_ROW_H;
                        if (i >= 0 && i < nb) { if (i == s_setup_cur) load_it = 1; s_setup_cur = i; }
                    } else if (pad->tx >= mx && pad->ty >= SU_LIST_Y + 36 && pad->ty < SU_LIST_Y + 60) { touch_su = 0; s_setup_cur = nb; }
                    else if (pad->tx >= mx && pad->ty >= SU_LIST_Y + 80 && pad->ty < SU_LIST_Y + 104) { touch_su = 1; s_setup_cur = nb + 1; }
                    else if (pad->tx >= mx && pad->ty >= SU_LIST_Y + 106 && pad->ty < SU_LIST_Y + 130) {
                        s_setup_cur = nb + 2;
                        plat_audio_set_ahead(plat_audio_ahead() + (pad->tx >= mx + mw / 2 ? 1 : -1));
                        cfg_set_ahead(plat_audio_ahead());
                    }
                }
                if (touch_su >= 0) {
                    int v = (pad->tx - (mx + 16)) * 127 / (mw - 42); if (v < 0) v = 0; if (v > 127) v = 127;
                    if (touch_su == 0 && v != g_syn_master) syn_event(SEV_MASTER, 0, v, 0);
                    if (touch_su == 1) { if (v < 8) v = 8; if (v != g_syn_poly) syn_event(SEV_POLY, 0, v, 0); }
                }
            } else touch_su = -1;
        }
        if (load_it && s_setup_cur > 0 && s_sf_why[s_setup_cur - 1]) {   /* red entry: explain instead of loading */
            int why = s_sf_why[s_setup_cur - 1];
            toast(why == 1 ? "TOO BIG FOR THE MEMORY LEFT" : why == 2 ? "SF3: COMPRESSED, NOT SUPPORTED" : "NOT A SOUNDFONT");
            load_it = 0;
        }
        if (load_it) {
            char path[400];
            if (s_setup_cur == 0) snprintf(path, sizeof path, "app0:/sf/GeneralUser-GS.sf2");
            else snprintf(path, sizeof path, "%s/%s", s_sf_dir, s_sf_names[s_setup_cur - 1]);
            if (app_load_bank(path) == 0) { s_sf_loaded = s_setup_cur - 1; s_bn = 0; }
            else toast("THE BANK DID NOT LOAD");
            sf_rescan();   /* free memory has changed */
        }
        break;
    }
    case PG_PERFORM: {
        if (s_name_edit) {   /* name editor has the pad */
            if ((dn & SCE_CTRL_LEFT) && s_name_cur > 0) s_name_cur--;
            if ((dn & SCE_CTRL_RIGHT) && s_name_cur < PERF_NAME - 1) s_name_cur++;
            int cd = (dn & SCE_CTRL_UP) ? 1 : (dn & SCE_CTRL_DOWN) ? -1 : 0;
            if (cd) {
                const char *cs = NAME_CHARS; int nc = (int)strlen(cs);
                const char *at = strchr(cs, s_name_buf[s_name_cur]);
                int ci = at ? (int)(at - cs) : 0;
                s_name_buf[s_name_cur] = cs[(ci + nc + cd) % nc];
            }
            if (dn & SCE_CTRL_CROSS) {
                char nm[PERF_NAME + 1];
                name_trim(nm);
                int slot = s_perf_sel - perf_factory_count();
                if (slot >= 0 && perf_user_exists(slot)) perf_user_rename(slot, nm);
                if (s_perf_sel == s_perf_cur) snprintf(s_perf_name, sizeof s_perf_name, "%s", nm);
                s_name_edit = 0; perf_msg("NAMED");
            }
            if (dn & SCE_CTRL_CIRCLE) { s_name_edit = 0; o_used = 1; }
            break;
        }
        if (s_perf_col == 0) {   /* the zones: parts down, fields across, the list past the last field */
            if (nav & SCE_CTRL_UP) s_part = (s_part + SYN_PARTS - 1) % SYN_PARTS;
            if (nav & SCE_CTRL_DOWN) s_part = (s_part + 1) % SYN_PARTS;
            if ((nav & SCE_CTRL_LEFT) && s_zone_field > 0) s_zone_field--;
            if (nav & SCE_CTRL_RIGHT) { if (s_zone_field < 3) s_zone_field++; else s_perf_col = 1; }
        } else {                 /* the list */
            if (nav & SCE_CTRL_UP) s_perf_sel = (s_perf_sel + PERF_LIST_N - 1) % PERF_LIST_N;
            if (nav & SCE_CTRL_DOWN) s_perf_sel = (s_perf_sel + 1) % PERF_LIST_N;
            if (nav & SCE_CTRL_LEFT) s_perf_col = 0;
        }
        if (dn & SCE_CTRL_TRIANGLE) s_zone_field = (s_zone_field + 1) % 4;
        if (edit && s_perf_col == 0) {
            const SynPart *p = &g_parts[s_part];
            switch (s_zone_field) {
            case 0: syn_event(SEV_RX, s_part, (p->rx + 18 + edit) % 18, 0); break;
            case 1: { int v = p->low + edit; if (v < 0) v = 0; if (v > 127) v = 127; syn_event(SEV_LOW, s_part, v, 0); break; }
            case 2: { int v = p->high + edit; if (v < 0) v = 0; if (v > 127) v = 127; syn_event(SEV_HIGH, s_part, v, 0); break; }
            default: { int v = p->trans + edit; if (v < -24) v = -24; if (v > 24) v = 24; syn_event(SEV_TRANS, s_part, v + 24, 0); break; }
            }
        }
        /* right stick walks the list */
        {
            static int stick_ms = 0;
            int dy = pad->ry < 90 ? -1 : pad->ry > 166 ? 1 : 0;
            if (dy) { if (++stick_ms == 1 || stick_ms > 20) { s_perf_sel = (s_perf_sel + PERF_LIST_N + dy) % PERF_LIST_N; if (stick_ms > 20) stick_ms = 15; } }
            else stick_ms = 0;
        }
        if (o_tap) perf_load_sel();
        if (dn & SCE_CTRL_SQUARE) perf_save_sel();
        if (dn & SCE_CTRL_SELECT) {
            int slot = s_perf_sel - perf_factory_count();
            if (slot >= 0 && perf_user_exists(slot)) name_begin(perf_user_name(slot));
            else perf_msg("SAVE A USER SLOT FIRST");
        }
        if (dn & SCE_CTRL_CROSS) {
            const SynPart *p = &g_parts[s_part];
            int n = (p->low + p->high) / 2 + p->trans; if (n < 0) n = 0; if (n > 127) n = 127;
            s_aud_note = n; syn_event(SEV_NOTE_ON, s_part, n, 100);
        }
        if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
        /* touch: rows, the near end of the range bar, the CH field, the list, the buttons */
        {
            static int touch_end = -1;
            if (pad->touch) {
                int row = (pad->ty - ZONE_Y) / ZONE_H;
                if (tdown && pad->tx < 716 && pad->ty >= ZONE_Y && row >= 0 && row < SYN_PARTS) {
                    s_part = row; touch_end = -1;
                    if (pad->tx >= 178 && pad->tx < 218) { s_zone_field = 0; syn_event(SEV_RX, s_part, (g_parts[s_part].rx + 1) % 18, 0); }
                    else if (pad->tx >= BAR_X - 8 && pad->tx < BAR_X + BAR_W + 8) {
                        int n = (pad->tx - BAR_X) * 128 / BAR_W;
                        const SynPart *p = &g_parts[s_part];
                        touch_end = (abs(n - p->low) <= abs(n - p->high)) ? 1 : 2;
                        s_zone_field = touch_end;
                    }
                }
                if (touch_end > 0 && pad->ty >= ZONE_Y && pad->ty < ZONE_Y + SYN_PARTS * ZONE_H) {
                    int n = (pad->tx - BAR_X) * 128 / BAR_W; if (n < 0) n = 0; if (n > 127) n = 127;
                    const SynPart *p = &g_parts[s_part];
                    if (touch_end == 1 && n != p->low) syn_event(SEV_LOW, s_part, n, 0);
                    if (touch_end == 2 && n != p->high) syn_event(SEV_HIGH, s_part, n, 0);
                }
                if (tdown && pad->tx >= RIGHT_X) {
                    int cy = 44, ch = SCR_H - 40 - 12 - cy, by = cy + ch - 40;
                    if (pad->ty >= by && pad->ty < by + 28) {
                        if (pad->tx < RIGHT_X + 106) perf_load_sel(); else perf_save_sel();
                    } else if (pad->ty >= cy + LIST_Y - 46 && pad->ty < cy + LIST_Y - 46 + LIST_ROWS * 18) {
                        int total = PERF_LIST_N, first = s_perf_sel - LIST_ROWS / 2;
                        if (first > total - LIST_ROWS) first = total - LIST_ROWS;
                        if (first < 0) first = 0;
                        int i = first + (pad->ty - (cy + LIST_Y - 46)) / 18;
                        if (i >= 0 && i < total) s_perf_sel = i;
                    } else if (pad->ty >= cy + 26 && pad->ty < cy + 52) {
                        int slot = s_perf_sel - perf_factory_count();
                        if (slot >= 0 && perf_user_exists(slot)) name_begin(perf_user_name(slot));
                    }
                }
            } else touch_end = -1;
        }
        break;
    }
    case PG_MT32: {
        /* two columns of five rows: MODULE and EMULATION */
        int mcol = s_mt_row < MR_MODULE_ROWS ? 0 : 1;
        if (nav & SCE_CTRL_UP) s_mt_row = mcol == 0 ? (s_mt_row == 0 ? MR_MODULE_ROWS - 1 : s_mt_row - 1) : (s_mt_row == MR_ANALOG ? MR_COUNT - 1 : s_mt_row - 1);
        if (nav & SCE_CTRL_DOWN) s_mt_row = mcol == 0 ? (s_mt_row == MR_MODULE_ROWS - 1 ? 0 : s_mt_row + 1) : (s_mt_row == MR_COUNT - 1 ? MR_ANALOG : s_mt_row + 1);
        if ((nav & SCE_CTRL_RIGHT) && mcol == 0) s_mt_row = MR_ANALOG + (s_mt_row < MR_COUNT - MR_ANALOG ? s_mt_row : MR_COUNT - MR_ANALOG - 1);
        if ((nav & SCE_CTRL_LEFT) && mcol == 1) s_mt_row = s_mt_row - MR_ANALOG < MR_MODULE_ROWS ? s_mt_row - MR_ANALOG : MR_MODULE_ROWS - 1;
        /* X acts on the selected row: toggles flip, values step up; only the ROM
         * FOLDER row reopens the emulator */
        int act = edit != 0 || (dn & SCE_CTRL_CROSS) != 0;
        if (tdown) {   /* tap on a row selects it; tap on its value changes it */
            int cx = 16, cy = 244, cw = 300, ex = cx + cw + 12, ew = 300;
            if (pad->tx >= cx && pad->tx < cx + cw && pad->ty >= cy + 32 && pad->ty < cy + 32 + MR_MODULE_ROWS * 30) {
                s_mt_row = (pad->ty - (cy + 32)) / 30;
                if (pad->tx >= cx + cw / 2) act = 1;
            } else if (pad->tx >= ex && pad->tx < ex + ew && pad->ty >= cy + 31 && pad->ty < cy + 31 + 5 * 21) {
                s_mt_row = MR_ANALOG + (pad->ty - (cy + 31)) / 21;
                if (pad->tx >= ex + ew / 2) act = 1;
            }
        }
        if (act) {
            int d = edit ? edit : 1;
            switch (s_mt_row) {
            case MR_MODE: syn_event(SEV_MODE, 0, g_syn_mode == 1 ? 0 : 1, 0); break;
            case MR_REVERB: mt32_set_reverb(!g_mt32.reverb_on); break;
            case MR_GAIN: mt32_set_gain(g_mt32.gain + d * 4); break;
            case MR_MAP: mt32_set_map(!g_mt32.map_stock); syn_event(SEV_MT_MAP, 0, g_mt32.map_stock, 0); break;
            case MR_ANALOG: mt32_set_analog((g_mt32.analog + 4 + d) % 4); break;
            case MR_SRC: mt32_set_src((g_mt32.srcq + 4 + d) % 4); break;
            case MR_RENDER: mt32_set_renderer(!g_mt32.renderer); break;
            case MR_PARTIALS: mt32_set_partials((g_mt32.partials_cap ? g_mt32.partials_cap : 32) + d * 4); break;
            case MR_CORES: mt32_set_cores((g_mt32.cores ? g_mt32.cores : 1) + d); break;
            default: break;
            }
        }
        if (act && s_mt_row == MR_RESCAN && !edit) {   /* reopen with the emulation settings and return to the previous mode */
            int was_mt32 = g_syn_mode == 1;
            if (was_mt32) { syn_event(SEV_MODE, 0, 0, 0); plat_sleep_ms(30); }
            mt32_open("ux0:data/prism/mt32");
            if (was_mt32 && mt32_is_open()) syn_event(SEV_MODE, 0, 1, 0);
        }
        break;
    }
    case PG_FX: {
        /* three columns: reverb and chorus, EQ and output, the part sends */
        static const int LO[3] = { FXS_REV_SIZE, FXS_EQ_LO, FXS_PART_REV }, HI[3] = { FXS_CHO_LEVEL, FXS_MASTER, FXS_PART_CHO };
        int col = s_fx_sel < FXS_EQ_LO ? 0 : s_fx_sel < FXS_PART_REV ? 1 : 2;
        if (nav & SCE_CTRL_UP) s_fx_sel = s_fx_sel == LO[col] ? HI[col] : s_fx_sel - 1;
        if (nav & SCE_CTRL_DOWN) s_fx_sel = s_fx_sel == HI[col] ? LO[col] : s_fx_sel + 1;
        if (nav & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
            int r = s_fx_sel - LO[col], nc = (col + 3 + ((nav & SCE_CTRL_RIGHT) ? 1 : -1)) % 3;
            if (r > HI[nc] - LO[nc]) r = HI[nc] - LO[nc];
            s_fx_sel = LO[nc] + r;
        }
        if (dn & SCE_CTRL_TRIANGLE) s_part = (s_part + 1) % SYN_PARTS;
        if (edit) fx_set(s_fx_sel, s_fx_sel == FXS_LIMITER ? (edit > 0 ? 127 : 0) : fx_get(s_fx_sel) + edit * 2);
        if (dn & SCE_CTRL_CROSS) { s_aud_note = s_part == 9 ? 36 : 60; syn_event(SEV_NOTE_ON, s_part, s_aud_note, 100); }
        if (!(b & SCE_CTRL_CROSS) && s_aud_note >= 0) { syn_event(SEV_NOTE_OFF, s_part, s_aud_note, 0); s_aud_note = -1; }
        {
            static int touch_fx = -1;
            if (pad->touch) {
                if (tdown) {
                    touch_fx = -1;
                    for (int k = 0; k < FXS_COUNT; k++) {
                        int x, y, w;
                        fx_slider_pos(k, &x, &y, &w);
                        if (pad->tx >= x - 8 && pad->tx < x + w + 8 && pad->ty >= y + 8 && pad->ty < y + 38) { touch_fx = k; s_fx_sel = k; }
                    }
                }
                if (touch_fx >= 0) {
                    int x, y, w;
                    fx_slider_pos(touch_fx, &x, &y, &w);
                    int v = (pad->tx - x) * 127 / (w - 10);
                    if (v != fx_get(touch_fx)) fx_set(touch_fx, v);
                }
            } else touch_fx = -1;
        }
        break;
    }
    default:
        break;
    }
    if (s_perf_msg_ms > 0) s_perf_msg_ms--;
}

void ui_frame(const UiStatus *st, const PlatPad *pad, int draw)
{
    if (s_page != s_page_prev) {
        if (s_page == PG_SETUP) sf_rescan();
        if (s_page != PG_PLAY) s_browse = 0;
        s_page_prev = s_page;
    }
    handle_input(pad, st);
    if (!draw) return;
    gfx_target(plat_fb_begin());
    gfx_clear(C_BG);
    switch (s_page) {
    case PG_PLAY:
        topbar(st, g_syn_mode == 1 && mt32_is_open() ? "MT-32 MODULE" : "SOUND MODULE");
        draw_play(st);
        if (s_browse) { if (g_syn_mode == 1 && mt32_is_open()) draw_browser_mt32(); else draw_browser(); }
        break;
    case PG_MIX: topbar(st, "MIX"); draw_mix(st); break;
    case PG_SETUP: topbar(st, "SETUP"); draw_setup(st); break;
    case PG_PERFORM: topbar(st, "PERFORM"); draw_perform(st); break;
    case PG_FX: topbar(st, "EFFECTS"); draw_fx(st); break;
    case PG_MT32: topbar(st, "MT-32 MODE"); draw_mt32(st); break;
    default: break;
    }
    tabbar();
    plat_fb_flip();
}
