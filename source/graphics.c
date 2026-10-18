#include <gba_video.h>
#include <gba_dma.h>
#include <gba_systemcalls.h>
#include "graphics.h"
#include "vn_assets.h"

static volatile uint16_t *page = (volatile uint16_t *)0x0600A000;
static int visible_page;
static bool current_title_palette, pending_title_palette;
static uint16_t scene_cache[19200] __attribute__((section(".ewram"), aligned(4)));
static unsigned cached_scene = ~0u;
static unsigned cached_portrait;
static uint16_t panel_pairs[65536] __attribute__((section(".ewram"), aligned(4)));
static bool panel_title_palette;
static uint16_t reading_cache[19200] __attribute__((section(".ewram"), aligned(4)));
static unsigned reading_scene = ~0u;
static unsigned reading_portrait;
static const uint16_t *reading_speaker;
static unsigned pending_darkness;

static void transparent_blit(int x, int y, int width, int height, const uint16_t *image)
    __attribute__((section(".iwram"), long_call, target("arm")));

static inline __attribute__((always_inline)) void pixel(int x, int y, unsigned color) {
    if (x < 0 || x >= 240 || y < 0 || y >= 160) return;
    int index = (y * 240 + x) / 2;
    uint16_t old = page[index];
    page[index] = (x & 1) ? ((old & 255) | (color << 8)) : ((old & 0xff00) | color);
}

void graphics_init(void) {
    REG_DISPCNT = MODE_4 | BG2_ON | LCDC_OFF;
    dmaCopy(vn_palette, (void *)BG_COLORS, 512);
    visible_page = 0;
    current_title_palette = pending_title_palette = false;
    page = (volatile uint16_t *)0x0600A000;
    cached_scene = ~0u;
    reading_scene = ~0u;
    panel_title_palette = false;
    pending_darkness = 0;
    REG_BLDCNT = REG_BLDY = 0;
    for (unsigned i = 0; i < 65536; ++i) {
        panel_pairs[i] = vn_panel_blend[i & 255] | (vn_panel_blend[i >> 8] << 8);
    }
}

void graphics_scene(unsigned scene, unsigned portrait) {
    pending_darkness = 0;
    pending_title_palette = false;
    if (scene == cached_scene && portrait == cached_portrait) {
        dmaCopy(scene_cache, (void *)page, 38400);
        return;
    }
    dmaCopy(vn_backgrounds[scene], (void *)page, 38400);
    if (portrait) {
        const PortraitAsset *p=&vn_portraits[portrait];
        transparent_blit(p->x,p->y,p->w,p->h,p->pixels);
    }
    dmaCopy((const void *)page, scene_cache, 38400);
    cached_scene = scene;
    cached_portrait = portrait;
}

void graphics_animated_scene(unsigned scene, unsigned portrait, int offset_x, unsigned darkness) {
    /* Reuse the cached background; do not invalidate static dialogue compositions. */
    graphics_scene(scene, 0);
    if (portrait) {
        const PortraitAsset *p = &vn_portraits[portrait];
        transparent_blit((int)p->x + offset_x, p->y, p->w, p->h, p->pixels);
    }
    pending_darkness = darkness > 16 ? 16 : darkness;
}

static void transparent_blit(int x, int y, int width, int height, const uint16_t *image) {
    int left = x < 0 ? 0 : x, right = x + width > 240 ? 240 : x + width;
    int top = y < 0 ? 0 : y, bottom = y + height > 160 ? 160 : y + height;
    if (left >= right || top >= bottom) return;
    for (int py = top; py < bottom; ++py) {
        const uint16_t *row = image + (py-y)*width/2;
        int col = left;
        if (col & 1) {
            int sx = col-x;
            unsigned color = (row[sx/2] >> ((sx&1)*8)) & 255;
            if (color) pixel(col,py,color);
            ++col;
        }
        volatile uint16_t *dest = page + (py*240+col)/2;
        unsigned sx = (unsigned)(col-x);
        const uint16_t *source = row + sx/2;
        bool shifted = sx & 1;
        for (; col+1 < right; col+=2, ++dest) {
            uint16_t pair = *source++;
            if (shifted) pair = (pair >> 8) | (*source << 8);
            if (pair & 255) {
                if (pair >> 8) *dest = pair;
                else *dest = (*dest & 65280) | pair;
            } else if (pair >> 8) *dest = (*dest & 255) | pair;
        }
        if (col < right) {
            int sx = col-x;
            unsigned color = (row[sx/2] >> ((sx&1)*8)) & 255;
            if (color) pixel(col,py,color);
        }
    }
}

void graphics_title(bool unlocked, unsigned selected) {
    pending_darkness = 0;
    pending_title_palette = true;
    dmaCopy(vn_title_backgrounds[unlocked ? 1 : 0], (void *)page, 38400);
    for (unsigned row = unlocked ? 0 : 1; row < 7; ++row)
        transparent_blit(VN_MENU_X, VN_MENU_Y + row * VN_MENU_SPACING,
                         VN_BUTTON_W, VN_BUTTON_H, vn_title_buttons[row][row == selected]);
}

void graphics_rect(int x, int y, int width, int height, unsigned char color) {
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > 240) width = 240 - x;
    if (y + height > 160) height = 160 - y;
    for (int row = y; row < y + height; ++row) {
        int col = x;
        if (col & 1) pixel(col++, row, color);
        for (; col + 1 < x + width; col += 2)
            page[(row * 240 + col) / 2] = color | (color << 8);
        if (col < x + width) pixel(col, row, color);
    }
}

static __attribute__((section(".iwram"), long_call, target("arm"))) void glyph(int x, int y, uint16_t id, unsigned color, unsigned scale) {
    for (int row = 0; row < 12; ++row)
        for (int col = 0; col < vn_widths[id]; ++col)
            if (vn_glyphs[id][row] & (1 << col)) {
                if (scale == 1) pixel(x + col, y + row, color);
                else graphics_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
}

/* Mode 4 has indexed pixels: blend through the scene's precomputed palette map. */
static bool rounded_inside(int x, int y, int w, int h, int radius) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    int dx = x < radius ? radius - x : x >= w - radius ? x - (w - radius - 1) : 0;
    int dy = y < radius ? radius - y : y >= h - radius ? y - (h - radius - 1) : 0;
    return dx * dx + dy * dy <= radius * radius;
}

void graphics_panel(int x, int y, int width, int height) {
    const uint8_t *blend = pending_title_palette ? vn_title_panel_blend : vn_panel_blend;
    if (panel_title_palette != pending_title_palette) {
        for (unsigned i = 0; i < 65536; ++i)
            panel_pairs[i] = blend[i & 255] | (blend[i >> 8] << 8);
        panel_title_palette = pending_title_palette;
    }
    for (int row = 0; row < height; ++row) {
        int py = y + row;
        if (py < 0 || py >= 160) continue;
        /* Only the five corner rows need circle tests, once per scanline. */
        int inset = 0;
        while (inset < width && !rounded_inside(inset, row, width, height, 5)) ++inset;
        int inner = 0;
        while (inner < width && !rounded_inside(inner - 1, row - 1, width - 2, height - 2, 4)) ++inner;
        int left = x + inset, right = x + width - inset;
        int start = x + inner, end = x + width - inner;
        if (inner >= width) { graphics_rect(left, py, right - left, 1, BORDER); continue; }
        graphics_rect(left, py, start - left, 1, BORDER);
        graphics_rect(end, py, right - end, 1, BORDER);
        if (start < 0) start = 0;
        if (end > 240) end = 240;
        if (start >= end) continue;
        if (start & 1) {
            unsigned old = page[(py * 240 + start) / 2] >> 8;
            pixel(start++, py, blend[old]);
        }
        volatile uint16_t *dest = page + (py * 240 + start) / 2;
        for (; start + 1 < end; start += 2, ++dest) *dest = panel_pairs[*dest];
        if (start < end) pixel(start, py, blend[*dest & 255]);
    }
}

void graphics_reading_scene(unsigned scene, unsigned portrait, const uint16_t *speaker) {
    pending_darkness = 0;
    pending_title_palette = false;
    if (reading_scene == scene && reading_portrait == portrait && reading_speaker == speaker) {
        dmaCopy(reading_cache, (void *)page, 38400);
        return;
    }
    graphics_scene(scene, portrait);
    graphics_panel(8, 87, graphics_text_width(speaker, 1) + 20, 14);
    graphics_text(14, 88, speaker, GOLD, 1);
    graphics_panel(8, 103, 224, 54);
    dmaCopy((const void *)page, reading_cache, 38400);
    reading_scene = scene; reading_portrait = portrait; reading_speaker = speaker;
}

int graphics_text_width(const uint16_t *text, unsigned scale) {
    int width = 0;
    for (; *text; ++text) if (*text != 65535) width += vn_widths[*text] * scale;
    return width;
}

unsigned graphics_text_length(const uint16_t *text) {
    unsigned length = 0;
    while (text[length]) ++length;
    return length;
}

void graphics_text(int x, int y, const uint16_t *text, unsigned color, unsigned scale) {
    int start_x = x;
    for (; *text; ++text) {
        if (*text == 65535) { x = start_x; y += 13 * scale; continue; }
        glyph(x, y, *text, color, scale);
        x += vn_widths[*text] * scale;
    }
}

void graphics_dialogue(const uint16_t *text, unsigned revealed) {
    int x = 12, y = 105;
    for (unsigned i = 0; text[i] && i < revealed; ++i) {
        if (text[i] == 65535) { x = 12; y += 13; continue; }
        if (x + vn_widths[text[i]] > 228) { x = 12; y += 13; }
        if (y > 131) break;
        glyph(x, y, text[i], WHITE, 1);
        x += vn_widths[text[i]];
    }
}

void graphics_present(void) {
    VBlankIntrWait();
    /* BG2 + backdrop as targets, darken mode. Registers change only during VBlank. */
    REG_BLDCNT = pending_darkness ? ((1 << 2) | (1 << 5) | (3 << 6)) : 0;
    REG_BLDY = pending_darkness;
    if (current_title_palette != pending_title_palette) {
        dmaCopy(pending_title_palette ? vn_title_palette : vn_palette, (void *)BG_COLORS, 512);
        current_title_palette = pending_title_palette;
    }
    visible_page ^= 1;
    REG_DISPCNT = MODE_4 | BG2_ON | (visible_page ? BACKBUFFER : 0);
    page = (volatile uint16_t *)(visible_page ? 0x06000000 : 0x0600A000);
}
