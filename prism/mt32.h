/* MT-32 mode. Wraps libmt32emu (Munt, LGPL). ROM images are read from
 * ux0:data/prism/mt32 and are not shipped. The audio thread owns the synth once
 * it is open; the panel reads a snapshot that the audio thread refreshes. */
#ifndef MT32_H
#define MT32_H
#include <stdint.h>

typedef struct {
    int open;                    /* synth open and ready */
    char lcd[24];                /* 20-character LCD text */
    int led;                     /* MIDI MESSAGE LED */
    unsigned int part_mask;      /* bit per part: 0..7 parts, 8 rhythm */
    char patch[9][12];           /* timbre name per part */
    int partials, partials_max;  /* partials in use, and the cap */
    char ctrl_desc[64], pcm_desc[64];
    char ctrl_file[80], pcm_file[80];   /* source files of the ROMs */
    char status[80];             /* status line */
    int analog;                  /* 0 digital only, 1 coarse, 2 accurate, 3 oversampled */
    int srcq;                    /* 32k to 48k conversion quality: 0 fastest, 1 fast, 2 good, 3 best */
    int renderer;                /* 0 integer renderer, 1 float */
    int partials_cap;            /* partial cap, 8..32 */
    int cores;                   /* render threads, 1..3 */
    int cores_used;              /* threads actually available */
    int selfcheck;               /* self-check result: 0x100 ran, bit 0 exp table exact, bit 1 clipper exact */
    int bench[4];                /* benchmark at open: full load on 1, 2 and 3 cores, then with voice stealing; percent of block budget */
    int reverb_on;
    int gain;                    /* output, 0..127 */
    int roms_found;              /* ROM files found */
    /* Part state for the panel. The channel map is read back from synth memory;
     * the rest mirrors what was last sent. */
    unsigned char chan[9];       /* MIDI channel 0..15, 16 = off */
    unsigned char prog[9];       /* last program sent */
    unsigned char vol[9], pan[9];
    char pname[128][12];         /* names of the 128 patches */
    int map_stock;               /* 1: stock map (parts on channels 2-9), 0: keyboard map (1-8) */
} Mt32State;

extern Mt32State g_mt32;

/* Main thread. Scans the folder, loads the ROMs, opens the synth. Returns 0 on success. */
int  mt32_open(const char *dir);
void mt32_close(void);
int  mt32_is_open(void);

/* Audio thread. */
void mt32_midi_packet(const unsigned char *pkt);   /* one USB-MIDI packet */
void mt32_render(short *stereo, int frames);
void mt32_snapshot(void);                          /* refresh g_mt32 */
void mt32_all_off(void);

/* Settings. Analog mode and the partial cap apply at the next open. */
void mt32_set_analog(int mode);
void mt32_set_reverb(int on);
void mt32_set_gain(int v127);
void mt32_set_map(int stock);        /* channel map used at the next open */
/* Applied at the next open: analog stage, rate converter, wave generator,
 * partial cap. */
void mt32_set_src(int q);
void mt32_set_renderer(int r);
void mt32_set_partials(int n);
void mt32_set_cores(int n);          /* applies immediately */
int  mt32_settings_stale(void);      /* nonzero if a next-open setting differs from the open synth */

/* Part control, audio thread only. The panel sends events. */
void mt32_apply_map(int stock);
void mt32_part_channel(int part, int ch);      /* ch 0..15, 16 = off; part 8 = rhythm */
void mt32_part_program(int part, int prog);    /* parts 0..7 only */
void mt32_part_cc(int part, int cc, int v);
void mt32_part_note(int part, int note, int vel);   /* vel 0 = note off */

#endif
