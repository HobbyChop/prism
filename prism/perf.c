#include "perf.h"
#include "plat.h"
#include "mt32.h"
extern int app_load_bank(const char *path);   /* main.c */
static int s_wanted_mode = -1;
#include "plat.h"
#include <stdio.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#define RX_OMNI 16
#define RX_OFF  17

/* ---- the factory set ------------------------------------------------ */
static void part_init(SynPart *p, int ch)
{
    memset(p, 0, sizeof *p);
    p->level = 100; p->pan = 64; p->cutoff = 64; p->attack = 64; p->release = 64;
    p->rx = (uint8_t)ch; p->low = 0; p->high = 127; p->trans = 0;
    p->rev = 40; p->cho = 0;
    p->prog = 0;
}
static void perf_init(Perf *pf, const char *name)
{
    pf->bank[0] = '*'; pf->bank[1] = 0;   /* keep whatever bank is loaded; the factory set is GM */
    memset(&pf->mod, 0, sizeof pf->mod);   /* no module settings */
    memset(pf, 0, sizeof *pf);
    snprintf(pf->name, sizeof pf->name, "%s", name);
    pf->master = 100; pf->poly = 48;
    fx_defaults(&pf->fx);
    for (int i = 0; i < SYN_PARTS; i++) part_init(&pf->parts[i], i);
}
static void zone(Perf *pf, int part, int ch, int prog, int low, int high, int trans, int level)
{
    SynPart *p = &pf->parts[part];
    p->rx = (uint8_t)ch; p->prog = (uint8_t)prog; p->low = (uint8_t)low; p->high = (uint8_t)high;
    p->trans = (int8_t)trans; p->level = (uint8_t)level;
}

static Perf s_factory[6];
static int s_factory_ready = 0;
static void factory_build(void)
{
    if (s_factory_ready) return;
    Perf *f = s_factory;
    perf_init(&f[0], "GM MULTI");                       /* sixteen plain channels */
    perf_init(&f[1], "GRAND + PAD");                    /* layer on channel 1 */
    zone(&f[1], 0, 0, 0, 0, 127, 0, 100);
    zone(&f[1], 1, 0, 89, 0, 127, 0, 68);
    f[1].parts[1].cho = 60; f[1].parts[1].rev = 70;
    perf_init(&f[2], "EP / BASS");                      /* split at C3 */
    zone(&f[2], 0, 0, 4, 48, 127, 0, 100);
    zone(&f[2], 1, 0, 33, 0, 47, 0, 110);
    perf_init(&f[3], "STRINGS+BRASS");                  /* brass above C3 */
    zone(&f[3], 0, 0, 48, 0, 127, 0, 96);
    zone(&f[3], 1, 0, 61, 48, 127, 0, 78);
    f[3].parts[0].rev = 80; f[3].fx.rev_size = 96;
    perf_init(&f[4], "ORGAN / BASS");
    zone(&f[4], 0, 0, 18, 48, 127, 0, 100);
    zone(&f[4], 1, 0, 32, 0, 47, 0, 108);
    perf_init(&f[5], "SYNTH STACK");                    /* lead, pad an octave down, bass below */
    zone(&f[5], 0, 0, 81, 48, 127, 0, 96);
    zone(&f[5], 1, 0, 89, 48, 127, -12, 60);
    zone(&f[5], 2, 0, 38, 0, 47, 0, 104);
    f[5].parts[1].cho = 80; f[5].parts[2].rev = 10;
    s_factory_ready = 1;
}
int perf_factory_count(void) { return 6; }
const Perf *perf_factory(int i) { factory_build(); return &s_factory[i < 0 ? 0 : i > 5 ? 5 : i]; }

/* ---- the user slots ---------------------------------------------------- */
/* File format PRF4: name[13], master, poly, bank file name (64 bytes), module
 * block (16 bytes), rack (12 bytes), then 15 bytes per part: prog bank level pan
 * cutoff reso attack release mute rx low high trans+24 rev cho. PRF3 (no module
 * block), PRF2 (no bank) and PRF1 (no rack, 13 bytes per part) still load. */
static char s_names[PERF_USER_SLOTS][PERF_NAME + 1];
static int s_exists[PERF_USER_SLOTS];

static void slot_path(int slot, char *out, int n)
{
    snprintf(out, n, "%sperformances/slot%02d.prf", g_app_dir, slot + 1);
}

void perf_scan(void)
{
    for (int s = 0; s < PERF_USER_SLOTS; s++) {
        char path[300];
        slot_path(s, path, sizeof path);
        s_exists[s] = 0; s_names[s][0] = 0;
        SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        if (fd < 0) continue;
        unsigned char h[4 + PERF_NAME + 1];
        if (sceIoRead(fd, h, sizeof h) == (int)sizeof h && (!memcmp(h, "PRF1", 4) || !memcmp(h, "PRF2", 4) || !memcmp(h, "PRF3", 4) || !memcmp(h, "PRF4", 4))) {
            memcpy(s_names[s], h + 4, PERF_NAME); s_names[s][PERF_NAME] = 0;
            s_exists[s] = 1;
        }
        sceIoClose(fd);
    }
}
int perf_user_exists(int slot) { return slot >= 0 && slot < PERF_USER_SLOTS && s_exists[slot]; }
const char *perf_user_name(int slot) { return perf_user_exists(slot) ? s_names[slot] : ""; }

int perf_user_load(int slot, Perf *out)
{
    char path[300];
    slot_path(slot, path, sizeof path);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    unsigned char h[4 + PERF_NAME + 1 + 2];
    int ok = sceIoRead(fd, h, sizeof h) == (int)sizeof h && (!memcmp(h, "PRF1", 4) || !memcmp(h, "PRF2", 4) || !memcmp(h, "PRF3", 4) || !memcmp(h, "PRF4", 4));
    int v2 = ok && h[3] >= '2', v3 = ok && h[3] >= '3', v4 = ok && h[3] >= '4';
    if (ok) {
        perf_init(out, "");
        memcpy(out->name, h + 4, PERF_NAME); out->name[PERF_NAME] = 0;
        out->master = h[4 + PERF_NAME + 1] > 127 ? 127 : h[4 + PERF_NAME + 1];
        out->poly = h[4 + PERF_NAME + 2] < 8 ? 8 : h[4 + PERF_NAME + 2];
        if (v3) {
            if (sceIoRead(fd, out->bank, 64) != 64) ok = 0;
            out->bank[63] = 0;
        }
        if (v4) {
            unsigned char mb[16];
            if (sceIoRead(fd, mb, 16) != 16) ok = 0;
            else {
                out->mod.valid = mb[0] ? 1 : 0; out->mod.mode = mb[1] ? 1 : 0; out->mod.reverb = mb[2] ? 1 : 0;
                out->mod.gain = mb[3] > 127 ? 127 : mb[3]; out->mod.analog = mb[4] & 3; out->mod.src = mb[5] & 3;
                out->mod.render = mb[6] ? 1 : 0; out->mod.partials = mb[7] < 8 ? 8 : mb[7] > 32 ? 32 : mb[7];
                out->mod.cores = mb[8] < 1 ? 1 : mb[8] > 3 ? 3 : mb[8]; out->mod.map = mb[9] ? 1 : 0;
            }
        }
        if (v2) {
            unsigned char fxb[12];
            if (sceIoRead(fd, fxb, 12) != 12) ok = 0;
            else {
                out->fx.rev_size = fxb[0] > 127 ? 127 : fxb[0]; out->fx.rev_damp = fxb[1] > 127 ? 127 : fxb[1]; out->fx.rev_level = fxb[2] > 127 ? 127 : fxb[2];
                out->fx.cho_rate = fxb[3] > 127 ? 127 : fxb[3]; out->fx.cho_depth = fxb[4] > 127 ? 127 : fxb[4]; out->fx.cho_level = fxb[5] > 127 ? 127 : fxb[5];
                out->fx.eq_lo = fxb[6] > 127 ? 127 : fxb[6]; out->fx.eq_mid = fxb[7] > 127 ? 127 : fxb[7]; out->fx.eq_hi = fxb[8] > 127 ? 127 : fxb[8];
                out->fx.limiter = fxb[9] ? 1 : 0;
            }
        }
        for (int i = 0; i < SYN_PARTS && ok; i++) {
            unsigned char b[15] = { 0 };
            if (sceIoRead(fd, b, v2 ? 15 : 13) != (v2 ? 15 : 13)) { ok = 0; break; }
            SynPart *p = &out->parts[i];
            p->prog = b[0] & 0x7F; p->bank = b[1]; p->level = b[2] > 127 ? 127 : b[2]; p->pan = b[3] > 127 ? 127 : b[3];
            p->cutoff = b[4] > 127 ? 127 : b[4]; p->reso = b[5] > 127 ? 127 : b[5];
            p->attack = b[6] > 127 ? 127 : b[6]; p->release = b[7] > 127 ? 127 : b[7];
            p->mute = b[8] ? 1 : 0; p->rx = b[9] > RX_OFF ? (uint8_t)i : b[9];
            p->low = b[10] > 127 ? 127 : b[10]; p->high = b[11] > 127 ? 127 : b[11];
            if (p->high < p->low) p->high = p->low;
            p->trans = (int8_t)((b[12] > 48 ? 48 : b[12]) - 24);
            if (v2) { p->rev = b[13] > 127 ? 127 : b[13]; p->cho = b[14] > 127 ? 127 : b[14]; }
        }
    }
    sceIoClose(fd);
    return ok ? 0 : -1;
}

int perf_user_save(int slot, const Perf *pf)
{
    char path[300];
    snprintf(path, sizeof path, "%sperformances", g_app_dir);
    sceIoMkdir(path, 0777);
    slot_path(slot, path, sizeof path);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return -1;
    unsigned char h[4 + PERF_NAME + 1 + 2];
    memcpy(h, "PRF4", 4);
    memset(h + 4, 0, PERF_NAME + 1);
    memcpy(h + 4, pf->name, strlen(pf->name) > PERF_NAME ? PERF_NAME : strlen(pf->name));
    h[4 + PERF_NAME + 1] = pf->master; h[4 + PERF_NAME + 2] = pf->poly;
    sceIoWrite(fd, h, sizeof h);
    {
        char bank[64];
        memset(bank, 0, sizeof bank);
        snprintf(bank, sizeof bank, "%.63s", pf->bank);
        sceIoWrite(fd, bank, sizeof bank);
    }
    {
        const PerfModule *m = &pf->mod;
        unsigned char mb[16] = { m->valid, m->mode, m->reverb, m->gain, m->analog, m->src, m->render, m->partials, m->cores, m->map, 0, 0, 0, 0, 0, 0 };
        sceIoWrite(fd, mb, 16);
    }
    {
        const FxParams *x = &pf->fx;
        unsigned char fxb[12] = { x->rev_size, x->rev_damp, x->rev_level, x->cho_rate, x->cho_depth, x->cho_level,
                                  x->eq_lo, x->eq_mid, x->eq_hi, x->limiter, 0, 0 };
        sceIoWrite(fd, fxb, 12);
    }
    for (int i = 0; i < SYN_PARTS; i++) {
        const SynPart *p = &pf->parts[i];
        unsigned char b[15] = { p->prog, p->bank, p->level, p->pan, p->cutoff, p->reso, p->attack, p->release,
                                p->mute, p->rx, p->low, p->high, (unsigned char)(p->trans + 24), p->rev, p->cho };
        sceIoWrite(fd, b, 15);
    }
    sceIoClose(fd);
    s_exists[slot] = 1;
    snprintf(s_names[slot], sizeof s_names[slot], "%s", pf->name);
    return 0;
}

void perf_user_rename(int slot, const char *name)
{
    Perf pf;
    if (perf_user_load(slot, &pf)) return;
    snprintf(pf.name, sizeof pf.name, "%.12s", name);
    perf_user_save(slot, &pf);
}

/* ---- the engine ------------------------------------------------------------ */
void perf_capture(Perf *out)
{
    perf_init(out, "");
    /* bank in use, as the file name the performance will ask for */
    if (!strcmp(g_syn_bank_name, PERF_BUILTIN_BANK)) out->bank[0] = 0;
    else snprintf(out->bank, sizeof out->bank, "%.63s", g_syn_bank_name);
    out->master = (uint8_t)g_syn_master;
    out->poly = (uint8_t)g_syn_poly;
    out->fx = g_fx;
    /* module state: engine in use and MT-32 settings */
    out->mod.valid = 1;
    out->mod.mode = (uint8_t)(g_syn_mode == 1 ? 1 : 0);
    out->mod.reverb = (uint8_t)(g_mt32.reverb_on ? 1 : 0);
    out->mod.gain = (uint8_t)(g_mt32.gain > 127 ? 127 : g_mt32.gain < 0 ? 0 : g_mt32.gain);
    out->mod.analog = (uint8_t)(g_mt32.analog & 3);
    out->mod.src = (uint8_t)(g_mt32.srcq & 3);
    out->mod.render = (uint8_t)(g_mt32.renderer ? 1 : 0);
    out->mod.partials = (uint8_t)(g_mt32.partials_cap ? g_mt32.partials_cap : 32);
    out->mod.cores = (uint8_t)(g_mt32.cores ? g_mt32.cores : 3);
    out->mod.map = (uint8_t)(g_mt32.map_stock ? 1 : 0);
    memcpy(out->parts, g_parts, sizeof out->parts);
    for (int i = 0; i < SYN_PARTS; i++) { out->parts[i].meter = 0; out->parts[i].notes = 0; }
}

int perf_apply(const Perf *pf)
{
    int r = 0;
    if (pf->bank[0] != '*') {   /* bank first, so the programs resolve in it */
        const char *want = pf->bank[0] ? pf->bank : PERF_BUILTIN_BANK;
        if (strcmp(want, g_syn_bank_name) != 0) {
            char path[400];
            if (pf->bank[0]) snprintf(path, sizeof path, "%ssoundfonts/%s", g_app_dir, pf->bank);
            else snprintf(path, sizeof path, PERF_BUILTIN_PATH);
            if (pf->bank[0] && plat_file_size(path) <= 0) r = 1;
            else if (app_load_bank(path) != 0) r = -1;
        }
    }
    g_fx = pf->fx;
    syn_perf_set(pf->parts, pf->master, pf->poly);
    s_wanted_mode = -1;
    if (pf->mod.valid) {   /* MT-32 settings; those needing a reopen are detected by mt32_settings_stale */
        mt32_set_reverb(pf->mod.reverb);
        mt32_set_gain(pf->mod.gain);
        mt32_set_analog(pf->mod.analog);
        mt32_set_src(pf->mod.src);
        mt32_set_renderer(pf->mod.render);
        mt32_set_partials(pf->mod.partials);
        mt32_set_cores(pf->mod.cores);
        mt32_set_map(pf->mod.map);
        if (mt32_is_open()) syn_event(SEV_MT_MAP, 0, pf->mod.map, 0);
        s_wanted_mode = pf->mod.mode ? 1 : 0;
    }
    return r;
}

int perf_wanted_mode(void) { return s_wanted_mode; }

int perf_exists_index(int idx)
{
    int nf = perf_factory_count();
    if (idx < 0) return 0;
    if (idx < nf) return 1;
    return perf_user_exists(idx - nf);
}

const char *perf_name_index(int idx)
{
    int nf = perf_factory_count();
    if (idx < 0) return "";
    if (idx < nf) return perf_factory(idx)->name;
    return perf_user_name(idx - nf);
}

int perf_load_index(int idx, Perf *out)
{
    int nf = perf_factory_count();
    if (idx < 0) return -1;
    if (idx < nf) { *out = *perf_factory(idx); return 0; }
    return perf_user_load(idx - nf, out);
}

const char *perf_bank_label(const Perf *pf, char *out, int n)
{
    if (pf->bank[0] == '*') snprintf(out, n, "ANY BANK");
    else if (!pf->bank[0]) snprintf(out, n, "GeneralUser GS");
    else {
        snprintf(out, n, "%s", pf->bank);
        char *dot = strrchr(out, '.');
        if (dot && !strcasecmp(dot, ".sf2")) *dot = 0;
    }
    return out;
}
