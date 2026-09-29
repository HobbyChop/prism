/* Platform layer for the PS Vita.
 * display: two 960 x 544 ABGR buffers in CDRAM, flipped on vblank; the panel
 * draws into the back buffer
 * audio: sceAudioOut, 48 kHz stereo, 256-frame blocks through a ring; the port
 * returns when a block is queued, not played
 * MIDI: VitaUsbMidi.skprx and its user shim, the PSP-MIDI adapter driver
 * power: a user-side power callback re-takes the USB port on wake
 * input: sceCtrl and the front touch panel
 * files: sceIo; user data in ux0:data/prism/
 * Notes: the kernel module must be loaded from its installed path, never app0:;
 * the app imports only the user shim; the module stays resident, so a build-
 * number check detects a stale one; closing from the LiveArea kills the process
 * without a shutdown, and module build 11 releases USB on its own. */
#include "plat.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <taihen.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

char g_app_dir[256];

/* imports from VitaUsbMidiUser.suprx (weak, bound when the shim loads) */
int umidiStart(void);
int umidiStop(void);
int umidiRead(unsigned int *pkt, unsigned int *ts);
int umidiWrite(unsigned int pkt);
int umidiStatus(void);
int umidiVersion(void);
int umidiRepresent(void);
int umidiStats(unsigned int *out6);
#define EXPECT_BUILD 11

#define TITLE_ID "PRISM0001"
static int s_clock_rc = 0;   /* result of the 444 MHz request */

/* ---- boot log ---- */
#define LOG_LINES 24
static char s_log[LOG_LINES][96];
static int s_log_n = 0;

void plat_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char *p = buf, *nl;
    while (p && *p) {
        nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) {
            if (s_log_n == LOG_LINES) { memmove(s_log[0], s_log[1], sizeof(s_log) - sizeof(s_log[0])); s_log_n--; }
            snprintf(s_log[s_log_n++], sizeof(s_log[0]), "%s", p);
        }
        p = nl ? nl + 1 : NULL;
    }
}
int plat_log_count(void) { return s_log_n; }
const char *plat_log_line(int i) { return (i >= 0 && i < s_log_n) ? s_log[i] : ""; }

/* ---- lifecycle ------------------------------------------------------- */
static uint32_t *s_fb[2];
static int s_back = 0;

void plat_init(void)
{
    s_clock_rc = scePowerSetArmClockFrequency(444);   /* refused on some firmware, which stays at 333 */
    scePowerSetBusClockFrequency(222);               /* memory bus at maximum */
    /* Panel on core 0 only. Core 1 is the audio thread's, core 2 the first
     * helper's. */
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), 0x10000);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    for (int i = 0; i < 2; i++) {
        SceUID blk = sceKernelAllocMemBlock("prism_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);
        void *base = NULL;
        if (blk >= 0) sceKernelGetMemBlockBase(blk, &base);
        s_fb[i] = (uint32_t *)base;
        if (base) memset(base, 0, 0x200000);
    }
    if (s_fb[0]) {
        SceDisplayFrameBuf fb = { sizeof(fb), s_fb[0], SCR_W, 0, SCR_W, SCR_H };
        sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    }
    s_back = 1;
    strcpy(g_app_dir, "ux0:data/prism/");
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/prism", 0777);
    sceIoMkdir("ux0:data/prism/soundfonts", 0777);
    sceIoMkdir("ux0:data/prism/performances", 0777);
    sceIoMkdir("ux0:data/prism/mt32", 0777);
}

int plat_cpu_mhz(int *rc) { if (rc) *rc = s_clock_rc; return scePowerGetArmClockFrequency(); }
void plat_sleep_ms(int ms) { sceKernelDelayThread(ms * 1000); }
unsigned int plat_time_us(void) { return sceKernelGetProcessTimeLow(); }
void plat_exit(void) { sceKernelExitProcess(0); }

int plat_thread_start(const char *name, int (*fn)(unsigned int, void *), int audio, int stack)
{
    /* audio thread: high priority on its own core; the panel stays on the main
     * thread */
    int priority = audio ? 0x41 : 0x10000100;   /* user thread priorities are 0x40..0xBF; 0x40 is reserved for the output thread */
    int affinity = audio ? 0x20000 : 0;
    SceUID thid = sceKernelCreateThread(name, (SceKernelThreadEntry)fn, priority,
                                        stack < 0x10000 ? 0x10000 : stack, 0, affinity, NULL);
    if (thid < 0) return thid;
    sceKernelStartThread(thid, 0, NULL);
    return 0;
}

/* ---- helper threads for the MT-32 emulator ---- */
/* Partials are shared between the caller (the audio thread on core 1) and up to
 * two helpers: one on core 2 and one on core 0 above the panel's priority. A run
 * wakes them by event flag, does the caller's share, then waits for theirs. */
#define PAR_MAX 3
static SceUID s_par_go[PAR_MAX], s_par_done = -1;
static void (*volatile s_par_fn)(void *, int) = NULL;
static void *volatile s_par_arg = NULL;
static int s_par_workers = 0;

static int par_worker(unsigned int args, void *argp)
{
    (void)args;
    int k = *(const int *)argp;
    for (;;) {
        unsigned int r = 0;
        sceKernelWaitEventFlag(s_par_go[k], 3, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &r, NULL);
        if (r & 2) break;
        void (*fn)(void *, int) = s_par_fn;
        if (fn) fn(s_par_arg, k);
        sceKernelSetEventFlag(s_par_done, 1u << k);
    }
    return 0;
}

int plat_par_start(int workers)
{
    static const int CORE[PAR_MAX] = { 0, 0x40000, 0x10000 };   /* helper 1 on core 2, helper 2 on core 0 */
    if (workers > PAR_MAX - 1) workers = PAR_MAX - 1;
    if (s_par_done < 0) s_par_done = sceKernelCreateEventFlag("prism_par_done", 0, 0, NULL);
    if (s_par_done < 0) return 0;
    for (int k = s_par_workers + 1; k <= workers; k++) {
        s_par_go[k] = sceKernelCreateEventFlag("prism_par_go", 0, 0, NULL);
        if (s_par_go[k] < 0) break;
        SceUID th = sceKernelCreateThread("prism_par", (SceKernelThreadEntry)par_worker, 0x41, 0x10000, 0, CORE[k], NULL);
        if (th < 0) break;
        int arg = k;
        sceKernelStartThread(th, sizeof arg, &arg);
        s_par_workers = k;
    }
    return s_par_workers;
}

int plat_par_workers(void) { return s_par_workers; }

/* Measurements run the caller on the audio core so the helpers join it as in
 * playback. */
void plat_bench_core(int on) { sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), on ? 0x20000 : 0x10000); }

void plat_par_run(void (*fn)(void *, int), void *arg, int count)
{
    int extra = count - 1;
    if (extra > s_par_workers) extra = s_par_workers;
    if (extra < 0) extra = 0;
    s_par_fn = fn;
    s_par_arg = arg;
    unsigned int mask = 0;
    for (int k = 1; k <= extra; k++) { mask |= 1u << k; sceKernelSetEventFlag(s_par_go[k], 1); }
    fn(arg, 0);
    if (mask) {
        unsigned int r = 0;
        sceKernelWaitEventFlag(s_par_done, mask, SCE_EVENT_WAITAND | SCE_EVENT_WAITCLEAR_PAT, &r, NULL);
    }
}

/* ---- audio ---- */
/* The render thread fills a ring that an output thread drains into the port. It
 * may run up to s_ahead blocks ahead, so a slow block is covered by blocks
 * already rendered. Each block ahead adds 5.3 ms of latency. */
#define RING_MAX 8
static int s_port = -1;
static short s_ring[RING_MAX][OUT_FRAMES * 2];
static volatile int s_ring_w = 0, s_ring_r = 0, s_ring_count = 0;
static volatile int s_ahead = 3;
static SceUID s_ev_space = -1, s_ev_data = -1, s_out_thread = -1;
static short s_direct[2][OUT_FRAMES * 2];
static int s_direct_i = 0;

static int out_thread(unsigned int args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        while (s_ring_count <= 0) {
            unsigned int r = 0;
            sceKernelWaitEventFlag(s_ev_data, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &r, NULL);
        }
        sceAudioOutOutput(s_port, s_ring[s_ring_r]);   /* returns once queued; this paces the render thread */
        s_ring_r = (s_ring_r + 1) % RING_MAX;
        __sync_fetch_and_sub(&s_ring_count, 1);
        sceKernelSetEventFlag(s_ev_space, 1);
    }
    return 0;
}

void plat_audio_set_ahead(int blocks)
{
    s_ahead = blocks < 1 ? 1 : blocks > RING_MAX - 1 ? RING_MAX - 1 : blocks;
    if (s_ev_space >= 0) sceKernelSetEventFlag(s_ev_space, 1);
}
int plat_audio_ahead(void) { return s_ahead; }

int plat_audio_open(void)
{
    s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, OUT_FRAMES, OUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (s_port >= 0) {
        int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
        sceAudioOutSetVolume(s_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
        s_ev_space = sceKernelCreateEventFlag("prism_out_space", 0, 0, NULL);
        s_ev_data = sceKernelCreateEventFlag("prism_out_data", 0, 0, NULL);
        /* output thread on the audio core, above the render thread */
        s_out_thread = sceKernelCreateThread("prism_out", (SceKernelThreadEntry)out_thread, 0x40, 0x4000, 0, 0x20000, NULL);
        if (s_out_thread >= 0 && s_ev_space >= 0 && s_ev_data >= 0) sceKernelStartThread(s_out_thread, 0, NULL);
        else s_out_thread = -1;
    }
    return s_port;
}

/* Fallback: without an output thread, or if it stops draining, write to the port
 * directly. */
static void audio_write_direct(const short *interleaved)
{
    short *slot = s_direct[s_direct_i];
    s_direct_i ^= 1;
    memcpy(slot, interleaved, sizeof(s_direct[0]));
    sceAudioOutOutput(s_port, slot);
}

int plat_audio_out_thread_ok(void) { return s_out_thread >= 0; }

void plat_audio_write(const short *interleaved)
{
    if (s_out_thread < 0) { audio_write_direct(interleaved); return; }
    int waited = 0;
    while (s_ring_count >= s_ahead) {
        unsigned int r = 0;
        SceUInt to = 100000;   /* 100 ms, far longer than any block */
        if (sceKernelWaitEventFlag(s_ev_space, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &r, &to) < 0 && ++waited >= 3) {
            audio_write_direct(interleaved);
            return;
        }
    }
    memcpy(s_ring[s_ring_w], interleaved, sizeof(s_ring[0]));
    s_ring_w = (s_ring_w + 1) % RING_MAX;
    __sync_fetch_and_add(&s_ring_count, 1);
    sceKernelSetEventFlag(s_ev_data, 1);
}

void plat_vblank(void) { sceDisplayWaitVblankStart(); }

/* ---- MIDI ---- */
/* Two separate states: the calls are bound (module and shim loaded, build
 * matches; fixed at boot) and the port is held (takeover succeeded; lost on
 * sleep or a failed takeover, recovered by the sequence below). */
static int s_shim_ok = 0;          /* umidi* calls are bound */
static int s_midi_ok = 0;          /* port held */
static int s_fail_phase = 0;       /* failed takeover step reported by the module; -1 = stale module */
static unsigned int s_fail_err = 0;
static int s_rung = 0;             /* recovery step: 0 fresh, 1 re-presented, 2 re-taken or given up */
static int s_retakes = 0;          /* re-takes since the last link (max 2) */
static int s_timed = 0;            /* timed re-takes since the last link (max 3) */
static unsigned int s_retry_at = 0;     /* plat_time_us of the next timed re-take; 0 = none */
static unsigned int s_quiet_until = 0;  /* quiet period after a re-take while the adapter enumerates */
static int s_manual = 0;           /* ticks until a user request runs its second step */
static void midi_note_failure(int r);

int plat_midi_init(void)
{
    static const char *KPATHS[] = {
        "ux0:app/" TITLE_ID "/module/VitaUsbMidi.skprx",
        "ur0:app/" TITLE_ID "/module/VitaUsbMidi.skprx",
        "uma0:app/" TITLE_ID "/module/VitaUsbMidi.skprx",
    };
    plat_log("MIDI: loading the kernel module");
    SceUID mod = -1;
    for (unsigned int i = 0; i < sizeof(KPATHS) / sizeof(KPATHS[0]); i++) {
        mod = taiLoadStartKernelModule(KPATHS[i], 0, NULL, 0);
        if (mod >= 0 || (unsigned int)mod == 0x8002D013u) break;
    }
    if (mod < 0 && (unsigned int)mod != 0x8002D013u) {
        plat_log("MIDI: kernel module FAILED %08X (unsafe homebrew off?)", (unsigned int)mod);
        return mod;
    }
    SceUID umod = sceKernelLoadStartModule("app0:/module/VitaUsbMidiUser.suprx", 0, NULL, 0, NULL, NULL);
    if (umod < 0)
        umod = sceKernelLoadStartModule("ux0:app/" TITLE_ID "/module/VitaUsbMidiUser.suprx", 0, NULL, 0, NULL, NULL);
    if (umod < 0) {
        plat_log("MIDI: user shim FAILED %08X", (unsigned int)umod);
        return umod;
    }
    int ver = umidiVersion();
    if (ver != EXPECT_BUILD) {
        plat_log("MIDI: stale kernel module (build %d, want %d): REBOOT the Vita", ver, EXPECT_BUILD);
        s_fail_phase = -1;
        return -1;
    }
    s_shim_ok = 1;
    int r = umidiStart();
    if (r < 0) {
        midi_note_failure(r);
        plat_log("MIDI: driver start FAILED %08X at step %d, retrying in 5 s", (unsigned int)r, s_fail_phase);
        return r;
    }
    s_midi_ok = 1;
    plat_log("MIDI: ready, build %d", ver);
    return 0;
}

int plat_midi_ok(void) { return s_midi_ok; }

int plat_midi_read(unsigned char *pkt)
{
    unsigned int v, ts;
    if (!s_midi_ok || !umidiRead(&v, &ts)) return 0;
    pkt[0] = v & 0xFF; pkt[1] = (v >> 8) & 0xFF; pkt[2] = (v >> 16) & 0xFF; pkt[3] = (v >> 24) & 0xFF;
    return 1;
}

int plat_midi_write(const unsigned char *pkt)
{
    if (!s_midi_ok) return 0;
    return umidiWrite(pkt[0] | (pkt[1] << 8) | (pkt[2] << 16) | (pkt[3] << 24));
}

int plat_midi_connected(void) { return s_midi_ok ? (umidiStatus() & 1) : 0; }

void plat_midi_shutdown(void)
{
    if (s_shim_ok) umidiStop();
    s_midi_ok = 0;
}

/* ---- power ---- */
/* Sleep and wake are handled by a user-side power callback. On resume the system
 * has handed the USB port back to its own driver, so the module releases and re-
 * takes it. The kernel module also listens for suspend events, but they never
 * arrived on the tested firmware. Callbacks are delivered when this thread
 * checks for them, which plat_power_tick does once a second. A SYSTEM suspend is
 * latched and acted on at the matching SYSTEM resume. The app-resume
 * notification is not used: it fires at launch and focus, and a stop/start
 * without a sleep put the MTP driver on the cable and brought up the PlayStation
 * prompt. */
static volatile int s_resume_pending = 0, s_suspended = 0;
static SceUID s_power_cb = -1;

static int power_cb(int notifyId, int notifyCount, int notifyArg, void *common)
{
    (void)notifyId; (void)notifyCount; (void)common;
    if (notifyArg & SCE_POWER_CB_SYSTEM_SUSPEND) s_suspended = 1;
    if ((notifyArg & SCE_POWER_CB_SYSTEM_RESUME) && s_suspended) { s_suspended = 0; s_resume_pending = 1; }
    return 0;
}

/* ---- USB recovery ---- */
/* A link is lost when sleep hands the port to the system, when a takeover fails
 * (another plugin or a stock driver holding the controller), or when the adapter
 * drops. Recovery steps, cheapest first:
 * 1. re-present: deactivate and reactivate the device. No stock driver touches
 * the cable.
 * 2. re-take: stop and start. The stock drivers hold the cable for 300 ms.
 * 3. timed re-takes 5, 10 and 20 s after a failure, then wait for the user (hold
 * SELECT or tap the MIDI lamp).
 * The main loop advances one step every two seconds while an established link
 * stays gone (plat_midi_represent). Wake runs a re-take directly. Steps and
 * timed re-takes are counted so a dead port never toggles the stock drivers
 * repeatedly; a link or a user request resets the counts. */
static void midi_note_failure(int r)
{
    unsigned int st[6] = { 0, 0, 0, 0, 0, 0 };
    umidiStats(st);
    s_fail_phase = (int)st[4];
    s_fail_err = st[3] ? st[3] : (unsigned int)r;
    static const unsigned int WAIT_S[3] = { 5, 10, 20 };
    s_retry_at = 0;
    if (s_timed < 3) {
        s_retry_at = plat_time_us() + WAIT_S[s_timed] * 1000000u;
        if (!s_retry_at) s_retry_at = 1;
    }
}

static int midi_retake(void)
{
    s_midi_ok = 0;
    umidiStop();
    plat_sleep_ms(300);
    int r = umidiStart();
    if (r >= 0) {
        s_midi_ok = 1; s_fail_phase = 0; s_fail_err = 0; s_retry_at = 0; s_rung = 0;
        s_quiet_until = plat_time_us() + 3000000u;
        if (!s_quiet_until) s_quiet_until = 1;
    } else {
        s_rung = 2;
        midi_note_failure(r);
    }
    return r;
}

static void midi_reset(void)
{
    s_rung = 0; s_retakes = 0; s_timed = 0; s_retry_at = 0; s_quiet_until = 0;
}

/* one step per call */
static void midi_ladder(void)
{
    if (!s_shim_ok) return;
    if (s_quiet_until && (int)(plat_time_us() - s_quiet_until) < 0) return;
    s_quiet_until = 0;
    if (s_rung == 0 && s_midi_ok && (umidiStatus() & 8)) { umidiRepresent(); s_rung = 1; return; }
    if (s_retakes < 2) { s_retakes++; midi_retake(); }
    else s_rung = 2;
}

/* main loop: an established link has been gone for two seconds */
void plat_midi_represent(void) { midi_ladder(); }

/* user request: reset the backoff, run the first step now and the next on the
 * following tick */
void plat_midi_reconnect(void)
{
    if (!s_shim_ok) return;
    midi_reset();
    s_manual = 2;
    midi_ladder();
}

/* 0 no driver (see the boot log), 1 port not held, 2 held and waiting for the
 * adapter, 3 linked */
int plat_midi_link(void)
{
    if (!s_shim_ok) return 0;
    int st = umidiStatus();
    if (!s_midi_ok || !(st & 8)) return 1;
    return (st & 1) ? 3 : 2;
}

/* Last failure: takeover step (1 deactivate, 2 start controller, 3 start driver,
 * 4 activate, 9 not registered; -1 stale module), error code, and seconds to the
 * next timed re-take (-1 none). */
int plat_midi_fail(int *phase, unsigned int *err, int *retry_in_s)
{
    if (phase) *phase = s_fail_phase;
    if (err) *err = s_fail_err;
    int s = -1;
    if (s_retry_at) { int d = (int)(s_retry_at - plat_time_us()); s = d > 0 ? (d + 999999) / 1000000 : 0; }
    if (retry_in_s) *retry_in_s = s;
    return s_fail_phase != 0 || s_fail_err != 0;
}

/* called once a second from plat_power_tick */
static void midi_tick(void)
{
    if (!s_shim_ok) return;
    if (s_resume_pending) { s_resume_pending = 0; midi_retake(); return; }
    int linked = s_midi_ok && (umidiStatus() & 1);
    if (linked) { midi_reset(); s_manual = 0; return; }
    if (s_manual > 0 && --s_manual == 0) { midi_ladder(); return; }
    if (s_retry_at && (int)(plat_time_us() - s_retry_at) >= 0) { s_retry_at = 0; s_timed++; midi_retake(); }
}

void plat_power_tick(void)
{
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    if (s_power_cb < 0) {
        s_power_cb = sceKernelCreateCallback("prism_power", 0, power_cb, NULL);
        if (s_power_cb >= 0) scePowerRegisterCallback(s_power_cb);
    }
    sceKernelCheckCallback();
    midi_tick();
}
int plat_batt_pct(void) { return scePowerGetBatteryLifePercent(); }
int plat_batt_charging(void) { return scePowerIsBatteryCharging() || scePowerIsPowerOnline(); }

/* ---- input ---- */
void plat_input_read(PlatPad *p)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    p->buttons = pad.buttons;
    p->lx = pad.lx; p->ly = pad.ly; p->rx = pad.rx; p->ry = pad.ry;
    SceTouchData td;
    memset(&td, 0, sizeof(td));
    p->touch = 0; p->tx = 0; p->ty = 0;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) >= 0 && td.reportNum > 0) {
        p->touch = 1;
        p->tx = td.report[0].x / 2;   /* touch panel coordinates are 1920 x 1088 */
        p->ty = td.report[0].y / 2;
    }
}

/* ---- display ---- */
uint32_t *plat_fb_begin(void) { return s_fb[s_back]; }

void plat_fb_flip(void)
{
    if (!s_fb[s_back]) return;
    SceDisplayFrameBuf fb = { sizeof(fb), s_fb[s_back], SCR_W, 0, SCR_W, SCR_H };
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    s_back ^= 1;
}

/* ---- files ---- */
int plat_list_files(const char *dir, const char *ext, char names[][64], int max)
{
    int n = 0;
    SceUID d = sceIoDopen(dir);
    if (d < 0) return 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    size_t el = strlen(ext);
    while (n < max && sceIoDread(d, &ent) > 0) {
        size_t l = strlen(ent.d_name);
        if (ent.d_name[0] != '.' && !SCE_S_ISDIR(ent.d_stat.st_mode) && l > el &&
            !strcasecmp(ent.d_name + l - el, ext)) {
            snprintf(names[n++], 64, "%.63s", ent.d_name);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
    for (int i = 1; i < n; i++)   /* insertion sort, case-insensitive */
        for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
            char t[64];
            memcpy(t, names[j], 64); memcpy(names[j], names[j - 1], 64); memcpy(names[j - 1], t, 64);
        }
    return n;
}

long plat_file_size(const char *path)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    return (long)st.st_size;
}

int plat_file_head(const char *path, void *buf, int n)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    int r = sceIoRead(fd, buf, (SceSize)n);
    sceIoClose(fd);
    return r;
}

/* ---- large allocations ---- */
/* Bank sample data is the one large allocation, held while the bank is loaded.
 * It comes from system user memory as its own block rather than from the 128 MB
 * newlib heap, so the heap does not fragment and the free memory reported by the
 * system is the budget for the next bank. Blocks are 4 KB granular, type
 * USER_RW. */
#define BIG_MIN   (1024 * 1024)
#define BIG_SLOTS 8
#define MEM_USER_RW 0x0C20D060
static struct { SceUID uid; void *base; unsigned int size; } s_big[BIG_SLOTS];

long plat_mem_free(void)
{
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (sceKernelGetFreeMemorySize(&info) < 0) return -1;
    return (long)info.size_user;
}

void *plat_bigalloc(unsigned int size)
{
    if (size < BIG_MIN) return malloc(size);
    for (int i = 0; i < BIG_SLOTS; i++) {
        if (s_big[i].base) continue;
        unsigned int sz = (size + 0xFFFu) & ~0xFFFu;
        SceUID uid = sceKernelAllocMemBlock("prism_bank", MEM_USER_RW, sz, NULL);
        if (uid < 0) return NULL;
        void *base = NULL;
        if (sceKernelGetMemBlockBase(uid, &base) < 0 || !base) { sceKernelFreeMemBlock(uid); return NULL; }
        s_big[i].uid = uid; s_big[i].base = base; s_big[i].size = sz;
        return base;
    }
    return NULL;
}

void plat_bigfree(void *p)
{
    if (!p) return;
    for (int i = 0; i < BIG_SLOTS; i++) {
        if (s_big[i].base != p) continue;
        sceKernelFreeMemBlock(s_big[i].uid);
        s_big[i].base = NULL; s_big[i].uid = -1; s_big[i].size = 0;
        return;
    }
    free(p);
}

void *plat_bigrealloc(void *p, unsigned int size)
{
    if (!p) return plat_bigalloc(size);
    for (int i = 0; i < BIG_SLOTS; i++) {
        if (s_big[i].base != p) continue;
        if (size <= s_big[i].size) return p;
        void *q = plat_bigalloc(size);
        if (!q) return NULL;
        memcpy(q, p, s_big[i].size);
        plat_bigfree(p);
        return q;
    }
    return realloc(p, size);
}
