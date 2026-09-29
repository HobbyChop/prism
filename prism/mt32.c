#define MT32EMU_API_TYPE 1
#include <mt32emu/mt32emu.h>
#include "mt32.h"
#include "plat.h"
#include <stdio.h>
#include <string.h>

Mt32State g_mt32;

static mt32emu_context s_ctx = NULL;
/* Defaults chosen for the Vita's CPU while keeping the LA32 model intact: no
 * analog stage, linear rate conversion, the bit-exact integer generator, all
 * partials. */
static int s_analog = MT32EMU_AOM_DIGITAL_ONLY;
static int s_srcq = MT32EMU_SRCQ_FASTEST;
static int s_renderer = 0;
static int s_partials = MT32EMU_DEFAULT_MAX_PARTIALS;
/* the patched Munt renders partials on threads supplied by the host */
extern void (*mt32emu_parallel_run)(void (*job)(void *, int), void *arg, int count);
extern int mt32emu_parallel_count;
extern int mt32emu_abort_chunk;
extern int mt32emu_prism_selfcheck;
static int s_cores = 3;
static int s_open_analog = -1, s_open_srcq = -1, s_open_renderer = -1, s_open_partials = -1;   /* settings the open synth was built with */
static int s_reverb = 1;
static float s_gain = 1.0f;
static int s_map_stock = 1;   /* stock channel map (parts on 2-9) unless the panel selects 1-8 */

/* synth memory addresses are the packed form of the sysex address */
static unsigned int memaddr(unsigned int a) { return ((a & 0x7F0000) >> 2) | ((a & 0x7F00) >> 1) | (a & 0x7F); }

/* DT1 sysex write into synth memory */
static void sysex_write(unsigned int addr, const unsigned char *data, int n)
{
    unsigned char m[16 + 64];
    if (!s_ctx || n < 1 || n > 64) return;
    int k = 0, sum = 0;
    m[k++] = 0xF0; m[k++] = 0x41; m[k++] = 0x10; m[k++] = 0x16; m[k++] = 0x12;
    m[k] = (addr >> 16) & 0x7F; sum += m[k++];
    m[k] = (addr >> 8) & 0x7F; sum += m[k++];
    m[k] = addr & 0x7F; sum += m[k++];
    for (int i = 0; i < n; i++) { m[k] = data[i] & 0x7F; sum += m[k++]; }
    m[k++] = (unsigned char)((128 - (sum & 127)) & 127);
    m[k++] = 0xF7;
    mt32emu_play_sysex_now(s_ctx, m, (mt32emu_bit32u)k);
}

/* names of the 128 patches: each patch refers to a timbre */
static void read_patch_names(void)
{
    if (!s_ctx) return;
    for (int n = 0; n < 128; n++) {
        unsigned char pp[8];
        char nm[16];
        mt32emu_read_memory(s_ctx, memaddr(0x050000) + (unsigned int)n * 8, 8, pp);
        nm[0] = 0;
        if (pp[0] > 2 || !mt32emu_get_sound_name(s_ctx, nm, pp[0], pp[1])) snprintf(nm, sizeof nm, "?");
        nm[10] = 0;
        snprintf(g_mt32.pname[n], sizeof g_mt32.pname[n], "%s", nm);
    }
}

/* sysex arrives in three-byte pieces and is passed whole to the emulator */
static unsigned char s_syx[8192];
static int s_syx_n = 0;

int mt32_is_open(void) { return s_ctx != NULL && g_mt32.open; }

static void set_status(const char *s) { snprintf(g_mt32.status, sizeof g_mt32.status, "%s", s); }

/* Benchmark at open: all partials busy, rendered on one, two and three cores,
 * then with a note stolen every block. Percent of the block budget. The caller
 * runs on the audio core for the measurement. */
static int bench_blocks(short *buf, int blocks)
{
    const unsigned long long blk_us = OUT_FRAMES * 1000000ull / OUT_RATE;
    unsigned int t0 = plat_time_us();
    for (int b = 0; b < blocks; b++) mt32emu_render_bit16s(s_ctx, (mt32emu_bit16s *)buf, OUT_FRAMES);
    unsigned int dt = plat_time_us() - t0;
    return (int)((unsigned long long)dt * 100ull / ((unsigned long long)blocks * blk_us));
}

static void bench(void)
{
    static short buf[OUT_FRAMES * 2];
    int saved = mt32emu_parallel_count, have = 1 + plat_par_workers();
    plat_bench_core(1);
    for (int p = 0; p < 8; p++)
        for (int i = 0; i < 4; i++)
            mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0x90 | g_mt32.chan[p]) | ((mt32emu_bit32u)(48 + p * 2 + i * 5) << 8) | (100u << 16));
    bench_blocks(buf, 8);   /* let the attacks settle */
    for (int c = 1; c <= 3; c++) {
        mt32emu_parallel_count = c > have ? have : c;
        g_mt32.bench[c - 1] = bench_blocks(buf, 16);
    }
    mt32emu_parallel_count = 3 > have ? have : 3;
    {   /* voice stealing: a new note every block on top of the full load */
        const unsigned long long blk_us = OUT_FRAMES * 1000000ull / OUT_RATE;
        unsigned int t0 = plat_time_us();
        for (int b = 0; b < 16; b++) {
            mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0x90 | g_mt32.chan[b & 7]) | ((mt32emu_bit32u)(72 + b) << 8) | (100u << 16));
            mt32emu_render_bit16s(s_ctx, (mt32emu_bit16s *)buf, OUT_FRAMES);
        }
        unsigned int dt = plat_time_us() - t0;
        g_mt32.bench[3] = (int)((unsigned long long)dt * 100ull / (16ull * blk_us));
    }
    for (int ch = 0; ch < 16; ch++) {
        mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0xB0 | ch) | (123u << 8));
        mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0xB0 | ch) | (120u << 8));
    }
    bench_blocks(buf, 24);   /* let the releases finish */
    mt32emu_parallel_count = saved;
    plat_bench_core(0);
    plat_log("MT-32 bench: 1 core %d%%, 2 cores %d%%, 3 cores %d%%, stealing %d%%",
             g_mt32.bench[0], g_mt32.bench[1], g_mt32.bench[2], g_mt32.bench[3]);
}

int mt32_open(const char *dir)
{
    mt32_close();
    memset(&g_mt32, 0, sizeof g_mt32);
    g_mt32.analog = s_analog; g_mt32.reverb_on = s_reverb; g_mt32.gain = (int)(s_gain * 100.0f);
    g_mt32.srcq = s_srcq; g_mt32.renderer = s_renderer; g_mt32.partials_cap = s_partials;
    g_mt32.cores = s_cores;
    g_mt32.partials_max = MT32EMU_DEFAULT_MAX_PARTIALS;
    strcpy(g_mt32.lcd, "");

    static char names[16][64];
    int n = plat_list_files(dir, ".rom", names, 16);
    int n2 = plat_list_files(dir, ".bin", names + n, 16 - n);
    n += n2;
    g_mt32.roms_found = n;
    if (n == 0) { set_status("No ROM files in ux0:data/prism/mt32"); return -1; }

    mt32emu_report_handler_i handler = { NULL };
    s_ctx = mt32emu_create_context(handler, NULL);
    if (!s_ctx) { set_status("The emulator would not start"); return -2; }

    int have_ctrl = 0, have_pcm = 0, unknown[16], nu = 0;
    const char *half_ctrl = NULL, *half_pcm = NULL;   /* first half of a split dump, for the name */
    for (int i = 0; i < n; i++) {
        char path[400];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        mt32emu_return_code rc = mt32emu_add_rom_file(s_ctx, path);
        if (rc == MT32EMU_RC_ADDED_CONTROL_ROM) {
            have_ctrl = 1;
            if (half_ctrl) snprintf(g_mt32.ctrl_file, sizeof g_mt32.ctrl_file, "%s + %s", half_ctrl, names[i]);
            else snprintf(g_mt32.ctrl_file, sizeof g_mt32.ctrl_file, "%s", names[i]);
        } else if (rc == MT32EMU_RC_ADDED_PCM_ROM) {
            have_pcm = 1;
            if (half_pcm) snprintf(g_mt32.pcm_file, sizeof g_mt32.pcm_file, "%s + %s", half_pcm, names[i]);
            else snprintf(g_mt32.pcm_file, sizeof g_mt32.pcm_file, "%s", names[i]);
        } else if (rc == MT32EMU_RC_ADDED_PARTIAL_CONTROL_ROM) half_ctrl = names[i];   /* partial ROM: wait for its pair */
        else if (rc == MT32EMU_RC_ADDED_PARTIAL_PCM_ROM) half_pcm = names[i];
        else if (nu < 16) unknown[nu++] = i;
    }
    /* split dumps: try every pair of unrecognised files */
    for (int a = 0; a < nu && !(have_ctrl && have_pcm); a++)
        for (int b = a + 1; b < nu; b++) {
            char p1[400], p2[400];
            snprintf(p1, sizeof p1, "%s/%s", dir, names[unknown[a]]);
            snprintf(p2, sizeof p2, "%s/%s", dir, names[unknown[b]]);
            mt32emu_return_code rc = mt32emu_merge_and_add_rom_files(s_ctx, p1, p2);
            if (rc == MT32EMU_RC_ADDED_CONTROL_ROM) { have_ctrl = 1; snprintf(g_mt32.ctrl_file, sizeof g_mt32.ctrl_file, "%s + %s", names[unknown[a]], names[unknown[b]]); }
            else if (rc == MT32EMU_RC_ADDED_PCM_ROM) { have_pcm = 1; snprintf(g_mt32.pcm_file, sizeof g_mt32.pcm_file, "%s + %s", names[unknown[a]], names[unknown[b]]); }
        }
    if (!have_ctrl || !have_pcm) {
        mt32emu_rom_info info;
        mt32emu_get_rom_info(s_ctx, &info);
        have_ctrl = info.control_rom_id != NULL;
        have_pcm = info.pcm_rom_id != NULL;
    }
    if (!have_ctrl || !have_pcm) {
        set_status(!have_ctrl && !have_pcm ? "ROMs not recognised: need a control and a PCM ROM"
                   : !have_ctrl ? "Control ROM missing (MT32_CONTROL.ROM or CM32L_CONTROL.ROM)"
                   : "PCM ROM missing (MT32_PCM.ROM or CM32L_PCM.ROM)");
        mt32emu_free_context(s_ctx); s_ctx = NULL;
        return -3;
    }
    mt32emu_set_analog_output_mode(s_ctx, (mt32emu_analog_output_mode)s_analog);
    mt32emu_select_renderer_type(s_ctx, s_renderer ? MT32EMU_RT_FLOAT : MT32EMU_RT_BIT16S);
    mt32emu_set_partial_count(s_ctx, (mt32emu_bit32u)s_partials);
    mt32emu_set_stereo_output_samplerate(s_ctx, (double)OUT_RATE);
    mt32emu_set_samplerate_conversion_quality(s_ctx, (mt32emu_samplerate_conversion_quality)s_srcq);
    mt32emu_parallel_run = plat_par_run;
    mt32emu_abort_chunk = 32;   /* abort passes of 32 samples: a stolen note starts up to 1 ms late */
    mt32_set_cores(s_cores);
    mt32emu_return_code rc = mt32emu_open_synth(s_ctx);
    if (rc != MT32EMU_RC_OK) {
        char b[80]; snprintf(b, sizeof b, "The synth would not open (%d)", (int)rc);
        set_status(b);
        mt32emu_free_context(s_ctx); s_ctx = NULL;
        return -4;
    }
    mt32emu_set_output_gain(s_ctx, s_gain);
    mt32emu_set_reverb_enabled(s_ctx, s_reverb ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE);
    /* apply every message at the start of the block: one render pass per block
     * instead of one per emulated serial byte */
    mt32emu_set_midi_delay_mode(s_ctx, MT32EMU_MDM_IMMEDIATE);
    for (int p = 0; p < 9; p++) { g_mt32.vol[p] = 100; g_mt32.pan[p] = 64; g_mt32.prog[p] = 0; }
    mt32_apply_map(s_map_stock);
    read_patch_names();
    {
        mt32emu_rom_info info;
        mt32emu_get_rom_info(s_ctx, &info);
        snprintf(g_mt32.ctrl_desc, sizeof g_mt32.ctrl_desc, "%s", info.control_rom_description ? info.control_rom_description : "?");
        snprintf(g_mt32.pcm_desc, sizeof g_mt32.pcm_desc, "%s", info.pcm_rom_description ? info.pcm_rom_description : "?");
        char b[80]; snprintf(b, sizeof b, "%.60s", g_mt32.ctrl_desc);
        set_status(b);
    }
    s_open_analog = s_analog; s_open_srcq = s_srcq; s_open_renderer = s_renderer; s_open_partials = s_partials;
    g_mt32.selfcheck = mt32emu_prism_selfcheck;
    plat_log("MT-32 self-check: %s", (mt32emu_prism_selfcheck & 0x103) == 0x103 ? "exponent table and clipper exact" : "MISMATCH");
    bench();
    s_syx_n = 0;
    g_mt32.open = 1;
    return 0;
}

void mt32_close(void)
{
    if (s_ctx) {
        g_mt32.open = 0;
        mt32emu_close_synth(s_ctx);
        mt32emu_free_context(s_ctx);
        s_ctx = NULL;
    }
}

void mt32_midi_packet(const unsigned char *pkt)
{
    if (!s_ctx || !g_mt32.open) return;
    int cin = pkt[0] & 0x0F;
    switch (cin) {
    case 0x4:   /* sysex start or continuation, three bytes */
        if (s_syx_n + 3 <= (int)sizeof s_syx) { s_syx[s_syx_n++] = pkt[1]; s_syx[s_syx_n++] = pkt[2]; s_syx[s_syx_n++] = pkt[3]; }
        break;
    case 0x5: case 0x6: case 0x7: {   /* sysex end with one, two or three bytes */
        int k = cin - 4;
        for (int i = 0; i < k && s_syx_n < (int)sizeof s_syx; i++) s_syx[s_syx_n++] = pkt[1 + i];
        if (s_syx_n >= 2 && s_syx[0] == 0xF0) mt32emu_play_sysex(s_ctx, s_syx, (mt32emu_bit32u)s_syx_n);
        s_syx_n = 0;
        break;
    }
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE: {
        mt32emu_bit32u msg = (mt32emu_bit32u)pkt[1] | ((mt32emu_bit32u)pkt[2] << 8) | ((mt32emu_bit32u)pkt[3] << 16);
        /* mirror host changes for the panel */
        if (cin == 0xC) { int c = pkt[1] & 0xF; for (int p = 0; p < 8; p++) if (g_mt32.chan[p] == c) g_mt32.prog[p] = pkt[2] & 0x7F; }
        else if (cin == 0xB && (pkt[2] == 7 || pkt[2] == 10)) {
            int c = pkt[1] & 0xF;
            for (int p = 0; p < 9; p++) if (g_mt32.chan[p] == c) { if (pkt[2] == 7) g_mt32.vol[p] = pkt[3] & 0x7F; else g_mt32.pan[p] = pkt[3] & 0x7F; }
        }
        mt32emu_play_msg(s_ctx, msg);
        break;
    }
    default: break;
    }
}

void mt32_render(short *stereo, int frames)
{
    if (!s_ctx || !g_mt32.open) { memset(stereo, 0, (size_t)frames * 2 * sizeof(short)); return; }
    mt32emu_render_bit16s(s_ctx, (mt32emu_bit16s *)stereo, (mt32emu_bit32u)frames);
}

void mt32_snapshot(void)
{
    if (!s_ctx || !g_mt32.open) return;
    char lcd[22];
    g_mt32.led = mt32emu_get_display_state(s_ctx, lcd, MT32EMU_BOOL_FALSE) ? 1 : 0;
    lcd[21] = 0;
    snprintf(g_mt32.lcd, sizeof g_mt32.lcd, "%s", lcd);
    g_mt32.part_mask = mt32emu_get_part_states(s_ctx);
    for (int p = 0; p < 9; p++) {
        const char *n = mt32emu_get_patch_name(s_ctx, (mt32emu_bit8u)p);
        snprintf(g_mt32.patch[p], sizeof g_mt32.patch[p], "%.10s", n ? n : "");
    }
    mt32emu_bit8u st[64];
    memset(st, 0, sizeof st);
    mt32emu_get_partial_states(s_ctx, st);
    int cnt = 0, pm = (int)mt32emu_get_partial_count(s_ctx);
    if (pm > 256) pm = 256;
    for (int i = 0; i < pm; i++) if ((st[i >> 2] >> ((i & 3) * 2)) & 3) cnt++;
    g_mt32.partials = cnt;
    g_mt32.partials_max = pm;
    g_mt32.reverb_on = mt32emu_is_reverb_enabled(s_ctx) ? 1 : 0;
    mt32emu_read_memory(s_ctx, memaddr(0x10000D), 9, g_mt32.chan);   /* channel map as held by the synth */
    static int names_tick = 0;
    if (++names_tick >= 128) { names_tick = 0; read_patch_names(); }   /* games can rewrite patches */
}

void mt32_set_map(int stock) { s_map_stock = stock ? 1 : 0; g_mt32.map_stock = s_map_stock; }

void mt32_apply_map(int stock)
{
    if (!s_ctx) { mt32_set_map(stock); return; }
    unsigned char ch[9];
    for (int p = 0; p < 8; p++) ch[p] = (unsigned char)(stock ? p + 1 : p);
    ch[8] = 9;
    sysex_write(0x10000D, ch, 9);
    memcpy(g_mt32.chan, ch, 9);
    mt32_set_map(stock);
}

void mt32_part_channel(int part, int ch)
{
    if (!s_ctx || part < 0 || part > 8) return;
    unsigned char v = (unsigned char)(ch < 0 ? 0 : ch > 16 ? 16 : ch);
    sysex_write(0x10000D + (unsigned int)part, &v, 1);
    g_mt32.chan[part] = v;
}

void mt32_part_program(int part, int prog)
{
    if (!s_ctx || part < 0 || part > 7) return;
    if (prog < 0) prog = 0;
    if (prog > 127) prog = 127;
    g_mt32.prog[part] = (unsigned char)prog;
    if (g_mt32.chan[part] < 16) mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0xC0 | g_mt32.chan[part]) | ((mt32emu_bit32u)prog << 8));
}

void mt32_part_cc(int part, int cc, int v)
{
    if (!s_ctx || part < 0 || part > 8) return;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    if (cc == 7) g_mt32.vol[part] = (unsigned char)v;
    else if (cc == 10) g_mt32.pan[part] = (unsigned char)v;
    if (g_mt32.chan[part] < 16) mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)(0xB0 | g_mt32.chan[part]) | ((mt32emu_bit32u)cc << 8) | ((mt32emu_bit32u)v << 16));
}

void mt32_part_note(int part, int note, int vel)
{
    if (!s_ctx || part < 0 || part > 8 || g_mt32.chan[part] >= 16) return;
    mt32emu_play_msg_now(s_ctx, (mt32emu_bit32u)((vel ? 0x90 : 0x80) | g_mt32.chan[part]) | ((mt32emu_bit32u)(note & 0x7F) << 8) | ((mt32emu_bit32u)(vel & 0x7F) << 16));
}

void mt32_all_off(void)
{
    if (!s_ctx || !g_mt32.open) return;
    for (int ch = 0; ch < 16; ch++) {
        mt32emu_play_msg(s_ctx, (mt32emu_bit32u)(0xB0 | ch) | (123u << 8));
        mt32emu_play_msg(s_ctx, (mt32emu_bit32u)(0xB0 | ch) | (120u << 8));
    }
}

void mt32_set_analog(int mode) { s_analog = mode < 0 ? 0 : mode > 3 ? 3 : mode; g_mt32.analog = s_analog; }
void mt32_set_src(int q) { s_srcq = q < 0 ? 0 : q > 3 ? 3 : q; g_mt32.srcq = s_srcq; }
void mt32_set_renderer(int r) { s_renderer = r ? 1 : 0; g_mt32.renderer = s_renderer; }
void mt32_set_partials(int n) { s_partials = n < 8 ? 8 : n > 32 ? 32 : n; g_mt32.partials_cap = s_partials; }
int mt32_settings_stale(void)
{
    if (!s_ctx || !g_mt32.open) return 0;
    return s_analog != s_open_analog || s_srcq != s_open_srcq || s_renderer != s_open_renderer || s_partials != s_open_partials;
}

void mt32_set_cores(int n)
{
    s_cores = n < 1 ? 1 : n > 3 ? 3 : n;
    g_mt32.cores = s_cores;
    int have = 1 + plat_par_workers();
    mt32emu_parallel_count = s_cores > have ? have : s_cores;
    g_mt32.cores_used = mt32emu_parallel_count;
}
void mt32_set_reverb(int on)
{
    s_reverb = on ? 1 : 0;
    g_mt32.reverb_on = s_reverb;
    if (s_ctx && g_mt32.open) mt32emu_set_reverb_enabled(s_ctx, s_reverb ? MT32EMU_BOOL_TRUE : MT32EMU_BOOL_FALSE);
}
void mt32_set_gain(int v127)
{
    if (v127 < 0) v127 = 0;
    if (v127 > 127) v127 = 127;
    s_gain = v127 / 100.0f;
    g_mt32.gain = v127;
    if (s_ctx && g_mt32.open) mt32emu_set_output_gain(s_ctx, s_gain);
}
