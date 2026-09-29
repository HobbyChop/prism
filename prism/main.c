/* PRISM, a sound module for the PS Vita.
 * Boot order: display, fonts, MIDI driver, audio thread, bank or saved state,
 * MT-32 ROMs, then the panel loop at 60 Hz. */
#include "plat.h"
#include "gfx.h"
#include "synth.h"
#include "ui.h"
#include "perf.h"
#include "mt32.h"
#include <stdio.h>
#include <string.h>
#include "cfg.h"

static int s_midi_rc = 1;

static Font *lf(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "app0:/fonts/%s.fnt", name);
    Font *f = font_load(path);
    if (!f) plat_log("font %s missing", name);
    return f;
}

static char s_bank_path[320];   /* path of the loaded bank, for restoring it */
static char s_load_head[96];     /* headline for the loading screen */

/* progress callback for the loading screen */
static void bank_progress(long done, long total)
{
    char d[48];
    if (total > 0) snprintf(d, sizeof d, "%ld of %ld MB", (done + 524288) >> 20, (total + 524288) >> 20);
    else d[0] = 0;
    ui_boot_progress(s_load_head, total > 0 ? (int)((unsigned long long)done * 100ull / (unsigned long long)total) : 0, d);
}
#define LOAD_MARGIN (4L << 20)  /* working memory for the loader */

/* Largest bank that can be loaded now: free memory plus the bank that would be
 * dropped, minus the loader margin. */
long app_bank_budget(void)
{
    long f = plat_mem_free();
    if (f < 0) return -1;
    return f + g_syn_bank_bytes - LOAD_MARGIN;
}

int app_load_bank(const char *path)
{
    plat_log("Loading %s", path);
    {
        const char *base = strrchr(path, '/');
        snprintf(s_load_head, sizeof s_load_head, "Loading %.60s", base ? base + 1 : path);
    }
    ui_boot_progress(s_load_head, 0, NULL);
    /* The new bank is loaded beside the old one and swapped in between blocks so
     * playback continues. If both will not fit, the old one is dropped first and
     * restored if the new one fails. */
    long need = plat_file_size(path), free_now = plat_mem_free();
    int dropped = 0;
    char prev[320];
    snprintf(prev, sizeof prev, "%s", s_bank_path);
    if (need > 0 && free_now >= 0 && need + LOAD_MARGIN > free_now && g_syn_bank_bytes > 0) {
        plat_log("  %ld MB free: dropping %s first", free_now >> 20, g_syn_bank_name);
        syn_unload();
        dropped = 1;
    }
    int r = syn_load(path);
    if (r) {
        plat_log("  FAILED: not a SoundFont, or out of memory (%ld MB free)", plat_mem_free() >> 20);
        if (dropped && prev[0]) {
            plat_log("  putting %s back", prev);
            if (syn_load(prev) != 0) s_bank_path[0] = 0;
        }
    } else {
        snprintf(s_bank_path, sizeof s_bank_path, "%s", path);
        plat_log("  %d presets, %ld MB free", syn_preset_count(), plat_mem_free() >> 20);
        cfg_set_bank(strcmp(g_syn_bank_name, PERF_BUILTIN_BANK) == 0 ? "" : g_syn_bank_name);   /* remembered for the next launch */
    }
    ui_boot_frame(r ? "The bank did not load" : "Ready");
    return r;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    plat_init();
    g_fonts.s11 = lf("sans11"); g_fonts.s12 = lf("sans12"); g_fonts.s13 = lf("sans13");
    g_fonts.m15 = lf("sansm15"); g_fonts.b17 = lf("sanss17"); g_fonts.b34 = lf("sanss34");
    g_fonts.mo10 = lf("mono10"); g_fonts.mo11 = lf("mono11"); g_fonts.mo12 = lf("mono12");
    g_fonts.mo14 = lf("monom14"); g_fonts.mo44 = lf("monom44");
    ui_boot_frame("Starting");

    s_midi_rc = plat_midi_init();
    ui_boot_frame("Starting");

    if (plat_thread_start("prism_audio", syn_audio_thread, 1, 0x20000) < 0) plat_log("audio thread failed");
    syn_set_progress(bank_progress);

    plat_log("Memory: %ld MB free for banks", plat_mem_free() >> 20);
    { int rc; int mhz = plat_cpu_mhz(&rc); plat_log("CPU: %d MHz%s", mhz, rc < 0 ? " (444 refused)" : ""); }
    cfg_load();
    plat_audio_set_ahead(cfg_ahead());
    perf_scan();
    /* boot question: reload the last state or start with the default GM set */
    int state = cfg_state(), use_state = 0;
    Perf state_pf;
    if (state >= 0 && perf_exists_index(state)) use_state = ui_boot_ask(perf_name_index(state));
    if (use_state && perf_load_index(state, &state_pf) == 0) {
        plat_log("State: reloading %s", state_pf.name[0] ? state_pf.name : "UNNAMED");
        int r = perf_apply(&state_pf);   /* bank, parts, rack, MT-32 settings */
        if (r == 1) plat_log("  its bank %s is gone: the built-in one instead", state_pf.bank);
        if (r == 1 || !g_syn_bank_name[0]) app_load_bank(PERF_BUILTIN_PATH);
    } else {
        use_state = 0;
        plat_log("State: the default GM set");
        app_load_bank(PERF_BUILTIN_PATH);
    }
    plat_log("Audio: output thread %s, %d blocks ahead", plat_audio_out_thread_ok() ? "up" : "MISSING (direct output)", plat_audio_ahead());
    plat_log("MT-32: %d helper cores", plat_par_start(2));
    plat_log("MT-32: looking for ROMs");
    ui_boot_frame("Starting");
    if (mt32_open("ux0:data/prism/mt32") == 0) plat_log("MT-32: %s", g_mt32.status);
    else plat_log("MT-32: %s", g_mt32.status);
    if (use_state && perf_wanted_mode() == 1) {   /* the state was saved in MT-32 mode */
        if (mt32_is_open()) syn_event(SEV_MODE, 0, 1, 0);
        else plat_log("State: wanted MT-32 mode, but no ROMs");
    }
    ui_init();
    if (use_state) ui_set_perf(state, state_pf.name);

    unsigned int frame = 0;
    int was_conn = 0, lost = 0;   /* USB link watchdog */
    int batt = plat_batt_pct(), chg = plat_batt_charging();
    for (;;) {
        PlatPad pad;
        plat_input_read(&pad);
        if ((frame % 60) == 0) { plat_power_tick(); batt = plat_batt_pct(); chg = plat_batt_charging(); }
        UiStatus st;
        st.midi_ok = plat_midi_ok();
        st.midi_connected = plat_midi_connected();
        st.midi_stale = (s_midi_rc == -1);
        st.midi_link = plat_midi_link();
        plat_midi_fail(&st.fail_phase, &st.fail_err, &st.retry_s);
        /* an established link that stays gone for two seconds (120 frames)
         * advances the reconnect sequence one step */
        if (was_conn && !st.midi_connected) lost = 1;
        else if (st.midi_connected) lost = 0;
        else if (lost) lost++;
        was_conn = st.midi_connected;
        if (lost >= 120) { lost = 1; plat_midi_represent(); }
        st.batt_pct = batt; st.batt_charging = chg;
        st.voices = g_syn_voices; st.poly = g_syn_poly; st.load = g_syn_load; st.load_avg = g_syn_load_avg;
        /* draw every other frame in MT-32 mode to leave core 0 to the helper thread */
        int draw = !(g_syn_mode == 1 && mt32_is_open() && (frame & 1));
        ui_frame(&st, &pad, draw);
        if (!draw) plat_vblank();
        frame++;
    }
    return 0;
}
