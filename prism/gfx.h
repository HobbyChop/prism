/* Drawing: alpha fills, rounded panels, discs and antialiased text from the
 * atlases produced by mkfont.py. Software rendering into a 960 x 544 ABGR
 * buffer. */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>

typedef struct Font Font;

void gfx_target(uint32_t *fb);
void gfx_clear(uint32_t rgb);
void gfx_fill(int x, int y, int w, int h, uint32_t rgb, int a);
void gfx_round(int x, int y, int w, int h, int r, uint32_t rgb, int a);   /* filled rounded rectangle */
void gfx_frame(int x, int y, int w, int h, int r, uint32_t rgb, int a);   /* one-pixel rounded outline */
void gfx_disc(int cx, int cy, int r, uint32_t rgb, int a);                /* antialiased edge */

Font *font_load(const char *path);
int   font_h(const Font *f);          /* line box */
int   font_ascent(const Font *f);
int   text_w(const Font *f, const char *s, int spacing);
/* y is the top of the line box; returns the x after the last glyph */
int   gfx_text(const Font *f, int x, int y, const char *s, uint32_t rgb, int a, int spacing);
void  gfx_text_right(const Font *f, int xr, int y, const char *s, uint32_t rgb, int a, int spacing);
void  gfx_text_center(const Font *f, int xc, int y, const char *s, uint32_t rgb, int a, int spacing);
/* draw at most w pixels wide, with an ellipsis if cut */
void  gfx_text_fit(const Font *f, int x, int y, int w, const char *s, uint32_t rgb, int a);

#endif
