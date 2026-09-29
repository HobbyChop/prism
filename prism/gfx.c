#include "gfx.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *s_fb;

void gfx_target(uint32_t *fb) { s_fb = fb; }

static inline uint32_t pack(uint32_t rgb)
{
    return 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);   /* ABGR */
}

static inline void blend(uint32_t *p, uint32_t rgb, int a)
{
    if (a >= 255) { *p = pack(rgb); return; }
    if (a <= 0) return;
    uint32_t d = *p;
    uint32_t dr = d & 0xFF, dg = (d >> 8) & 0xFF, db = (d >> 16) & 0xFF;
    uint32_t sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    dr += ((sr - dr) * a) >> 8; dg += ((sg - dg) * a) >> 8; db += ((sb - db) * a) >> 8;
    *p = 0xFF000000u | (db << 16) | (dg << 8) | dr;
}

void gfx_clear(uint32_t rgb)
{
    uint32_t c = pack(rgb);
    uint32_t *p = s_fb, *e = s_fb + SCR_W * SCR_H;
    while (p < e) { p[0] = c; p[1] = c; p[2] = c; p[3] = c; p += 4; }
}

void gfx_fill(int x, int y, int w, int h, uint32_t rgb, int a)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (a >= 255) {
        uint32_t c = pack(rgb);
        for (int j = 0; j < h; j++) {
            uint32_t *p = s_fb + (y + j) * SCR_W + x;
            for (int i = 0; i < w; i++) p[i] = c;
        }
        return;
    }
    for (int j = 0; j < h; j++) {
        uint32_t *p = s_fb + (y + j) * SCR_W + x;
        for (int i = 0; i < w; i++) blend(p + i, rgb, a);
    }
}

/* Coverage of pixel (px,py) inside a disc at (cx,cy) with radius r: signed
 * distance to the edge clamped to one pixel. */
static inline int disc_cov(int px, int py, float cx, float cy, float r)
{
    float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
    float d = __builtin_sqrtf(dx * dx + dy * dy);
    float c = r + 0.5f - d;
    if (c <= 0.0f) return 0;
    if (c >= 1.0f) return 255;
    return (int)(c * 255.0f);
}

void gfx_disc(int cx, int cy, int r, uint32_t rgb, int a)
{
    for (int py = cy - r - 1; py <= cy + r + 1; py++) {
        if (py < 0 || py >= SCR_H) continue;
        for (int px = cx - r - 1; px <= cx + r + 1; px++) {
            if (px < 0 || px >= SCR_W) continue;
            int c = disc_cov(px, py, (float)cx, (float)cy, (float)r);
            if (c) blend(s_fb + py * SCR_W + px, rgb, (c * a) >> 8);
        }
    }
}

void gfx_round(int x, int y, int w, int h, int r, uint32_t rgb, int a)
{
    if (r <= 0) { gfx_fill(x, y, w, h, rgb, a); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_fill(x + r, y, w - 2 * r, h, rgb, a);
    gfx_fill(x, y + r, r, h - 2 * r, rgb, a);
    gfx_fill(x + w - r, y + r, r, h - 2 * r, rgb, a);
    /* corners by coverage */
    const float cxs[2] = { x + r - 0.0f, x + w - r - 0.0f }, cys[2] = { y + r - 0.0f, y + h - r - 0.0f };
    for (int k = 0; k < 4; k++) {
        float cx = cxs[k & 1], cy = cys[k >> 1];
        int x0 = (k & 1) ? x + w - r : x, y0 = (k >> 1) ? y + h - r : y;
        for (int py = y0; py < y0 + r; py++) {
            if (py < 0 || py >= SCR_H) continue;
            for (int px = x0; px < x0 + r; px++) {
                if (px < 0 || px >= SCR_W) continue;
                int c = disc_cov(px, py, cx, cy, (float)r);
                if (c) blend(s_fb + py * SCR_W + px, rgb, (c * a) >> 8);
            }
        }
    }
}

void gfx_frame(int x, int y, int w, int h, int r, uint32_t rgb, int a)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_fill(x + r, y, w - 2 * r, 1, rgb, a);
    gfx_fill(x + r, y + h - 1, w - 2 * r, 1, rgb, a);
    gfx_fill(x, y + r, 1, h - 2 * r, rgb, a);
    gfx_fill(x + w - 1, y + r, 1, h - 2 * r, rgb, a);
    if (r <= 0) return;
    const float cxs[2] = { x + r - 0.0f, x + w - r - 0.0f }, cys[2] = { y + r - 0.0f, y + h - r - 0.0f };
    for (int k = 0; k < 4; k++) {
        float cx = cxs[k & 1], cy = cys[k >> 1];
        int x0 = (k & 1) ? x + w - r : x, y0 = (k >> 1) ? y + h - r : y;
        for (int py = y0; py < y0 + r; py++) {
            if (py < 0 || py >= SCR_H) continue;
            for (int px = x0; px < x0 + r; px++) {
                if (px < 0 || px >= SCR_W) continue;
                float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
                float d = __builtin_sqrtf(dx * dx + dy * dy) - (r - 0.5f);
                if (d < 0) d = -d;
                float c = 1.0f - d;
                if (c > 0) blend(s_fb + py * SCR_W + px, rgb, (int)(c * a));
            }
        }
    }
}

/* ---- text -------------------------------------------------------------- */
typedef struct { uint16_t x, y, w, h; int16_t xoff, yoff; uint16_t adv; } Glyph;   /* adv 8.8 */
struct Font {
    int height, ascent, first, count, aw, ah;
    Glyph g[96];
    uint8_t *atlas;
};

Font *font_load(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    unsigned char hdr[16];
    if (fread(hdr, 1, 16, fp) != 16 || memcmp(hdr, "PFNT", 4)) { fclose(fp); return NULL; }
    Font *f = (Font *)calloc(1, sizeof(Font));
    if (!f) { fclose(fp); return NULL; }
    f->height = hdr[4] | (hdr[5] << 8);
    f->ascent = hdr[6] | (hdr[7] << 8);
    f->first = hdr[8] | (hdr[9] << 8);
    f->count = hdr[10] | (hdr[11] << 8);
    f->aw = hdr[12] | (hdr[13] << 8);
    f->ah = hdr[14] | (hdr[15] << 8);
    if (f->count > 96) f->count = 96;
    for (int i = 0; i < f->count; i++) {
        unsigned char r[14];
        if (fread(r, 1, 14, fp) != 14) { free(f); fclose(fp); return NULL; }
        f->g[i].x = r[0] | (r[1] << 8); f->g[i].y = r[2] | (r[3] << 8);
        f->g[i].w = r[4] | (r[5] << 8); f->g[i].h = r[6] | (r[7] << 8);
        f->g[i].xoff = (int16_t)(r[8] | (r[9] << 8)); f->g[i].yoff = (int16_t)(r[10] | (r[11] << 8));
        f->g[i].adv = r[12] | (r[13] << 8);
    }
    f->atlas = (uint8_t *)malloc((size_t)f->aw * f->ah);
    if (!f->atlas || fread(f->atlas, 1, (size_t)f->aw * f->ah, fp) != (size_t)f->aw * f->ah) {
        free(f->atlas); free(f); fclose(fp); return NULL;
    }
    fclose(fp);
    return f;
}

int font_h(const Font *f) { return f ? f->height : 0; }
int font_ascent(const Font *f) { return f ? f->ascent : 0; }

int text_w(const Font *f, const char *s, int spacing)
{
    if (!f || !s) return 0;
    int x = 0, n = 0;
    for (; *s; s++, n++) {
        int c = (unsigned char)*s - f->first;
        if (c < 0 || c >= f->count) c = 0;
        x += f->g[c].adv;
    }
    return (x >> 8) + (n > 1 ? (n - 1) * spacing : 0);
}

int gfx_text(const Font *f, int x, int y, const char *s, uint32_t rgb, int a, int spacing)
{
    if (!f || !s) return x;
    int fx = x << 8;   /* 8.8 */
    int base = y + f->ascent;
    for (; *s; s++) {
        int c = (unsigned char)*s - f->first;
        if (c < 0 || c >= f->count) c = 0;
        const Glyph *g = &f->g[c];
        int gx = (fx >> 8) + g->xoff, gy = base + g->yoff;
        for (int j = 0; j < g->h; j++) {
            int py = gy + j;
            if (py < 0 || py >= SCR_H) continue;
            const uint8_t *row = f->atlas + (g->y + j) * f->aw + g->x;
            uint32_t *p = s_fb + py * SCR_W;
            for (int i = 0; i < g->w; i++) {
                int px = gx + i;
                if (px < 0 || px >= SCR_W) continue;
                int cov = row[i];
                if (cov) blend(p + px, rgb, (cov * a) >> 8);
            }
        }
        fx += g->adv + (spacing << 8);
    }
    return fx >> 8;
}

void gfx_text_right(const Font *f, int xr, int y, const char *s, uint32_t rgb, int a, int spacing)
{
    gfx_text(f, xr - text_w(f, s, spacing), y, s, rgb, a, spacing);
}

void gfx_text_center(const Font *f, int xc, int y, const char *s, uint32_t rgb, int a, int spacing)
{
    gfx_text(f, xc - text_w(f, s, spacing) / 2, y, s, rgb, a, spacing);
}

void gfx_text_fit(const Font *f, int x, int y, int w, const char *s, uint32_t rgb, int a)
{
    if (text_w(f, s, 0) <= w) { gfx_text(f, x, y, s, rgb, a, 0); return; }
    char buf[64];
    size_t n = strlen(s);
    if (n > 60) n = 60;
    while (n > 0) {
        memcpy(buf, s, n); buf[n] = 0; strcat(buf, "...");
        if (text_w(f, buf, 0) <= w) break;
        n--;
    }
    gfx_text(f, x, y, buf, rgb, a, 0);
}
