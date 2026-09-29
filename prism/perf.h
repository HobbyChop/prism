/* Performances: the whole module as one unit. Every part's program, mix, tone,
 * receive channel, key range and transpose, plus master, polyphony and the MT-32
 * settings. Six factory performances are built in; sixteen user slots live in
 * ux0:data/prism/performances/. */
#ifndef PERF_H
#define PERF_H
#include "synth.h"
#include "fx.h"

#define PERF_NAME 12
#define PERF_USER_SLOTS 16
#define PERF_BUILTIN_BANK "GeneralUser-GS.sf2"
#define PERF_BUILTIN_PATH "app0:/sf/GeneralUser-GS.sf2"

/* Module settings, saved with a performance since PRF4. valid = 0 in older
 * files, which leave the module settings unchanged. */
typedef struct {
    uint8_t valid, mode, reverb, gain, analog, src, render, partials, cores, map;
} PerfModule;

typedef struct {
    char name[PERF_NAME + 1];
    char bank[64];               /* soundfont file name in the folder; "" = built-in bank, "*" = keep whatever is loaded */
    uint8_t master, poly;
    PerfModule mod;
    FxParams fx;
    SynPart parts[SYN_PARTS];
} Perf;

int  perf_factory_count(void);
const Perf *perf_factory(int i);

void perf_scan(void);                        /* read the user slot names at boot */
int  perf_user_exists(int slot);
const char *perf_user_name(int slot);        /* "" when empty */
int  perf_user_load(int slot, Perf *out);    /* 0 = ok */
int  perf_user_save(int slot, const Perf *p);
void perf_user_rename(int slot, const char *name);

void perf_capture(Perf *out);                /* capture from the live engine */
int  perf_apply(const Perf *p);              /* apply to the engine, bank first. Returns 0 ok, 1 bank missing (rest applied), -1 bank failed to load. */
const char *perf_bank_label(const Perf *p, char *out, int n);   /* bank name for display */

/* list index as the panel orders it: factory entries first, then user slots */
int  perf_exists_index(int idx);
const char *perf_name_index(int idx);
int  perf_load_index(int idx, Perf *out);   /* 0 = ok */
/* engine requested by the last applied performance: -1 none, 0 GM, 1 MT-32 */
int  perf_wanted_mode(void);

#endif
