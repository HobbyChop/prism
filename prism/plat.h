/* Platform layer for the PS Vita: display, audio, MIDI, input, power, files.
 * Implemented in platform_vita.c. */
#ifndef PLAT_H
#define PLAT_H
#include <stdint.h>

#define SCR_W 960
#define SCR_H 544
#define OUT_RATE 48000
#define OUT_FRAMES 256          /* 5.3 ms blocks */

typedef struct {
    unsigned int buttons;       /* SCE_CTRL_* bits */
    int lx, ly, rx, ry;         /* 0..255, 128 = centre */
    int touch, tx, ty;          /* front touch panel, screen pixels */
} PlatPad;

extern char g_app_dir[256];     /* ux0:data/prism/ */

void plat_init(void);
void plat_log(const char *fmt, ...);   /* boot log */
int  plat_log_count(void);
const char *plat_log_line(int i);
void plat_sleep_ms(int ms);
unsigned int plat_time_us(void);
void plat_exit(void);
int  plat_thread_start(const char *name, int (*fn)(unsigned int, void *), int audio, int stack);
/* Helper threads sharing a job with the caller: job(arg, k) runs for k in [0, count). */
int  plat_par_start(int workers);       /* helper threads beyond the caller; returns the number created */
int  plat_par_workers(void);
void plat_par_run(void (*job)(void *, int), void *arg, int count);
void plat_bench_core(int on);           /* 1: move the caller to the audio core for a measurement; 0: move it back */

int  plat_audio_open(void);
void plat_audio_write(const short *interleaved);   /* OUT_FRAMES stereo frames */
void plat_audio_set_ahead(int blocks);              /* blocks the render thread may run ahead of the port, 1..7 */
int  plat_audio_ahead(void);
int  plat_audio_out_thread_ok(void);                 /* nonzero if the output thread exists; otherwise blocks go straight to the port */
void plat_vblank(void);                              /* wait for vblank without drawing */

int  plat_midi_init(void);
int  plat_midi_ok(void);
int  plat_midi_read(unsigned char *pkt);
int  plat_midi_write(const unsigned char *pkt);
int  plat_midi_connected(void);
/* USB port recovery after sleep, a failed takeover or an adapter drop */
void plat_midi_represent(void);     /* called by the main loop when an established link has dropped; advances one step */
void plat_midi_reconnect(void);     /* user request: run the whole recovery sequence */
int  plat_midi_link(void);          /* 0 no driver, 1 port lost, 2 waiting for the adapter, 3 linked */
int  plat_midi_fail(int *phase, unsigned int *err, int *retry_in_s);
void plat_midi_shutdown(void);
void plat_power_tick(void);
int  plat_batt_pct(void);
int  plat_batt_charging(void);
int  plat_cpu_mhz(int *set_rc);    /* current CPU clock, and the result of the 444 MHz request */

void plat_input_read(PlatPad *p);

uint32_t *plat_fb_begin(void);   /* back buffer, 960 x 544 ABGR */
void plat_fb_flip(void);

/* list files matching *ext in dir, sorted, up to max; returns the count */
int  plat_list_files(const char *dir, const char *ext, char names[][64], int max);
long plat_file_size(const char *path);
int  plat_file_head(const char *path, void *buf, int n);   /* read the first n bytes; returns the count or -1 */

/* Large allocations. Bank sample data lives in its own memory block outside the
 * newlib heap, so the free memory reported by the system is the budget for the
 * next bank. Small requests go to malloc. */
long  plat_mem_free(void);                        /* free user memory for such blocks, bytes */
void *plat_bigalloc(unsigned int size);
void  plat_bigfree(void *p);
void *plat_bigrealloc(void *p, unsigned int size);

#endif
