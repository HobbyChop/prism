/* Sound engine: TinySoundFont (MIT, with local patches) playing sixteen MIDI
 * parts. The audio thread owns the engine; the panel talks to it through events
 * and reads the part table. */
#ifndef SYNTH_H
#define SYNTH_H
#include <stdint.h>

#define SYN_PARTS 16

typedef struct {
    uint8_t prog, bank;          /* program 0..127 and bank MSB */
    uint8_t level, pan;          /* 0..127; defaults 100 and 64 */
    uint8_t cutoff, reso;        /* 64 = preset default */
    uint8_t attack, release;     /* 64 = preset default */
    uint8_t mute;
    uint8_t rev, cho;            /* send levels 0..127 (CC 91, 93) */
    uint8_t rx;                  /* receive channel 0..15; 16 = omni, 17 = off */
    uint8_t low, high;           /* key range */
    int8_t  trans;               /* transpose in semitones, -24..24 */
    uint8_t notes;               /* keys held */
    float meter;                 /* level meter 0..1, decaying */
    char name[24];               /* preset name */
} SynPart;

extern SynPart g_parts[SYN_PARTS];   /* written by the audio thread, read by the panel */
extern volatile int g_syn_voices;    /* active voices */
extern volatile int g_syn_poly;      /* voice cap */
extern volatile int g_syn_master;    /* 0..127 */
extern volatile int g_syn_load;      /* worst block time over the last second, percent of budget */
extern volatile int g_syn_load_avg;  /* mean block time over the same window, percent */
extern volatile int g_syn_mode;      /* 0 = SoundFont engine, 1 = MT-32 mode; owned by the audio thread */
extern char g_syn_bank_name[64];     /* file name of the loaded bank */
extern long g_syn_bank_bytes;        /* memory used by the bank (its file size); 0 = none */

/* Load a SoundFont on the main thread. The audio thread swaps it in at a block
 * boundary; the old bank is freed here. Returns 0 on success. */
int  syn_load(const char *path);
int  syn_ready(void);
int  syn_preset_count(void);
/* Preset info by index: bank and program numbers and the name. Returns 0 if idx
 * is out of range. */
int  syn_preset_info(int idx, int *bank, int *num, const char **name);
void syn_unload(void);               /* unload the current bank (silence) to make room for another */
typedef void (*SynProgress)(long done, long total);
void syn_set_progress(SynProgress cb);   /* progress callback for bank loading, called whenever the percentage changes */
const char *syn_preset_name_of(int part);

/* panel to engine events, any thread; drained by the audio thread */
enum { SEV_PROG = 1, SEV_LEVEL, SEV_PAN, SEV_CUT, SEV_RES, SEV_ATK, SEV_REL, SEV_MUTE,
       SEV_MASTER, SEV_NOTE_ON, SEV_NOTE_OFF, SEV_ALLOFF, SEV_POLY, SEV_BANK,
       SEV_RX, SEV_LOW, SEV_HIGH, SEV_TRANS /* a = trans + 24 */, SEV_REV, SEV_CHO,
       SEV_MODE /* a = 0 GM, 1 MT-32 */,
       /* MT-32 mode, part = 0..8 (8 = rhythm) */
       SEV_MT_CH /* a = 0..15, 16 off */, SEV_MT_PROG, SEV_MT_VOL, SEV_MT_PAN, SEV_MT_MAP /* a = stock */ };
#define SYN_RX_OMNI 16
#define SYN_RX_OFF  17
void syn_event(int kind, int part, int a, int b);
/* Stage a whole performance from the main thread. The audio thread takes it at a
 * block boundary after silencing everything. */
void syn_perf_set(const SynPart *parts, int master, int poly);

/* audio thread entry */
int  syn_audio_thread(unsigned int args, void *argp);

#endif
