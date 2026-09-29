/* Panel: the pages and the tab bar. Draws with gfx into the back buffer. Touch
 * first, with the pad as an alternative. */
#ifndef UI_H
#define UI_H
#include "gfx.h"
#include "plat.h"

typedef struct {
    Font *s11, *s12, *s13, *m15, *b17, *b34;      /* Plex Sans: regular, medium, semibold */
    Font *mo10, *mo11, *mo12, *mo14, *mo44;        /* Plex Mono: regular, medium */
} Fonts;
extern Fonts g_fonts;

typedef struct {
    int midi_ok, midi_connected, midi_stale;
    int midi_link, fail_phase, retry_s;   /* see plat_midi_link, plat_midi_fail */
    unsigned int fail_err;
    int batt_pct, batt_charging;
    int voices, poly, load, load_avg;
} UiStatus;

void ui_init(void);
void ui_frame(const UiStatus *st, const PlatPad *pad, int draw);   /* input, then the frame if draw is set */
void ui_boot_frame(const char *headline);                 /* boot screen with the log */
void ui_boot_progress(const char *headline, int pct, const char *detail);   /* boot screen with a progress bar */
int  ui_boot_ask(const char *state_name);   /* boot question: returns 1 to reload the state, 0 for the default GM set */
void ui_set_perf(int idx, const char *name);   /* set the performance shown as loaded */

#endif
