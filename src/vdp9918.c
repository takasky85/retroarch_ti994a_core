/* TMS9918A video display processor -- see vdp9918.h */
#include <string.h>
#include "vdp9918.h"

const uint32_t vdp9918_palette[16] = {
    0x000000, 0x000000, 0x21C842, 0x5EDC78,
    0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
    0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80,
    0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF
};

void vdp_reset(vdp9918 *v)
{
    memset(v->reg, 0, sizeof v->reg);
    v->status = 0;
    v->latch = 0;
    v->latch_data = 0;
    v->rdbuf = 0;
    v->addr = 0;
    v->write_mode = 0;
}

void vdp_write_data(vdp9918 *v, uint8_t d)
{
    v->latch = 0;
    v->rdbuf = d;
    v->vram[v->addr & 0x3FFFu] = d;
    v->addr = (uint16_t)((v->addr + 1u) & 0x3FFFu);
}

void vdp_write_ctrl(vdp9918 *v, uint8_t d)
{
    if (!v->latch) {
        v->latch_data = d;
        v->latch = 1;
        return;
    }
    v->latch = 0;
    if (d & 0x80u) {                                  /* register write */
        v->reg[d & 7u] = v->latch_data;
    } else {                                          /* address set */
        v->addr = (uint16_t)(((d & 0x3Fu) << 8) | v->latch_data);
        v->write_mode = (uint8_t)((d >> 6) & 1u);
        if (!(d & 0x40u)) {                           /* read set-up: prefetch */
            v->rdbuf = v->vram[v->addr];
            v->addr = (uint16_t)((v->addr + 1u) & 0x3FFFu);
        }
    }
}

uint8_t vdp_read_data(vdp9918 *v)
{
    uint8_t r = v->rdbuf;
    v->latch = 0;
    v->rdbuf = v->vram[v->addr & 0x3FFFu];
    v->addr = (uint16_t)((v->addr + 1u) & 0x3FFFu);
    return r;
}

uint8_t vdp_read_status(vdp9918 *v)
{
    uint8_t r = v->status;
    v->status &= 0x1Fu;                               /* clear F, 5S, C */
    v->latch = 0;
    return r;
}

int vdp_int_line(const vdp9918 *v)
{
    return (v->status & 0x80u) && (v->reg[1] & 0x20u);
}

void vdp_set_frame_flag(vdp9918 *v)
{
    v->status |= 0x80u;
}

/* ------------------------------------------------------------- rendering */

static void render_background(const vdp9918 *v, int y, uint8_t *line)
{
    const uint8_t *vr = v->vram;
    int m3 = (v->reg[0] & 2) != 0;
    int m1 = (v->reg[1] & 0x10) != 0;
    int m2 = (v->reg[1] & 0x08) != 0;
    int row = y >> 3, ln = y & 7, col, i;
    unsigned nt = (unsigned)(v->reg[2] & 0x0F) << 10;

    if (m1) {                                         /* text, 40 x 24, 6 px wide */
        unsigned pg = (unsigned)(v->reg[4] & 7) << 11;
        uint8_t fg = (uint8_t)(v->reg[7] >> 4), bg = (uint8_t)(v->reg[7] & 15);
        for (col = 0; col < 40; col++) {
            unsigned code = vr[(nt + (unsigned)row * 40u + (unsigned)col) & 0x3FFFu];
            unsigned pat = vr[(pg + code * 8u + (unsigned)ln) & 0x3FFFu];
            for (i = 0; i < 6; i++)
                line[8 + col * 6 + i] = (pat & (0x80u >> i)) ? fg : bg;
        }
    } else if (m2) {                                  /* multicolor, 64 x 48 blocks */
        unsigned pg = (unsigned)(v->reg[4] & 7) << 11;
        for (col = 0; col < 32; col++) {
            unsigned code = vr[(nt + (unsigned)row * 32u + (unsigned)col) & 0x3FFFu];
            unsigned pat = vr[(pg + code * 8u + (unsigned)(row & 3) * 2u + (unsigned)(ln >> 2)) & 0x3FFFu];
            uint8_t l = (uint8_t)(pat >> 4), r = (uint8_t)(pat & 15);
            for (i = 0; i < 4; i++) { line[col * 8 + i] = l; line[col * 8 + 4 + i] = r; }
        }
    } else if (m3) {                                  /* graphics II (bitmap) */
        unsigned colbase = (v->reg[3] & 0x80) ? 0x2000u : 0u;
        unsigned patbase = (v->reg[4] & 4) ? 0x2000u : 0u;
        unsigned colmask = ((unsigned)(v->reg[3] & 0x7F) << 3) | 7u;
        unsigned patmask = ((unsigned)(v->reg[4] & 3) << 8) | (colmask & 0xFFu);
        for (col = 0; col < 32; col++) {
            unsigned code = vr[(nt + (unsigned)row * 32u + (unsigned)col) & 0x3FFFu]
                          + (unsigned)(row >> 3) * 256u;
            unsigned pat = vr[(patbase + ((code & patmask) * 8u) + (unsigned)ln) & 0x3FFFu];
            unsigned cl  = vr[(colbase + ((code & colmask) * 8u) + (unsigned)ln) & 0x3FFFu];
            uint8_t fg = (uint8_t)(cl >> 4), bg = (uint8_t)(cl & 15);
            for (i = 0; i < 8; i++)
                line[col * 8 + i] = (pat & (0x80u >> i)) ? fg : bg;
        }
    } else {                                          /* graphics I */
        unsigned ct = ((unsigned)v->reg[3] << 6) & 0x3FFFu;
        unsigned pg = (unsigned)(v->reg[4] & 7) << 11;
        for (col = 0; col < 32; col++) {
            unsigned code = vr[(nt + (unsigned)row * 32u + (unsigned)col) & 0x3FFFu];
            unsigned pat = vr[(pg + code * 8u + (unsigned)ln) & 0x3FFFu];
            unsigned cl  = vr[(ct + (code >> 3)) & 0x3FFFu];
            uint8_t fg = (uint8_t)(cl >> 4), bg = (uint8_t)(cl & 15);
            for (i = 0; i < 8; i++)
                line[col * 8 + i] = (pat & (0x80u >> i)) ? fg : bg;
        }
    }
}

/* Draw the sprites that are on line y into 'line'.  Updates status (C, 5S). */
static void render_sprites(vdp9918 *v, int y, uint8_t *line, int limit)
{
    const uint8_t *vr = v->vram;
    int size16 = (v->reg[1] & 2) != 0;
    int mag = (v->reg[1] & 1) != 0;
    int extent = (size16 ? 16 : 8) << mag;
    unsigned sat = (unsigned)(v->reg[5] & 0x7F) << 7;
    unsigned spg = (unsigned)(v->reg[6] & 7) << 11;
    int visible[32], nvis = 0, i, k;
    uint8_t filled[VDP_WIDTH];   /* any sprite pixel (collision detection) */
    uint8_t colored[VDP_WIDTH];  /* a visible (non-transparent) sprite pixel already drawn */

    for (i = 0; i < 32; i++) {
        int sy = vr[(sat + (unsigned)i * 4u) & 0x3FFFu];
        if (sy == 0xD0) break;
        if (sy >= 0xE0) sy -= 256;
        sy += 1;
        if (y < sy || y >= sy + extent) continue;
        if (nvis >= limit) {
            if (!(v->status & 0x40u)) {
                v->status = (uint8_t)((v->status & 0xE0u) | 0x40u | (unsigned)i);
            }
            break;
        }
        visible[nvis++] = i;
    }
    if (!nvis) return;

    memset(filled, 0, sizeof filled);
    memset(colored, 0, sizeof colored);
    for (k = 0; k < nvis; k++) {
        unsigned base = (sat + (unsigned)visible[k] * 4u) & 0x3FFFu;
        int sy = vr[base];
        int sx = vr[base + 1];
        unsigned name = vr[base + 2];
        unsigned attr = vr[base + 3];
        uint8_t color = (uint8_t)(attr & 15);
        int row, px, w = size16 ? 16 : 8;

        if (sy >= 0xE0) sy -= 256;
        sy += 1;
        if (attr & 0x80u) sx -= 32;
        row = (y - sy) >> mag;
        if (size16) name &= 0xFCu;

        for (px = 0; px < w; px++) {
            unsigned pbyte, bit;
            int xx, m;
            if (size16) pbyte = vr[(spg + name * 8u + (unsigned)row + (px >= 8 ? 16u : 0u)) & 0x3FFFu];
            else        pbyte = vr[(spg + name * 8u + (unsigned)row) & 0x3FFFu];
            bit = (pbyte >> (7 - (px & 7))) & 1u;
            if (!bit) continue;
            for (m = 0; m <= mag; m++) {
                xx = sx + (px << mag) + m;
                if (xx < 0 || xx >= VDP_WIDTH) continue;
                if (filled[xx]) v->status |= 0x20u;           /* collision */
                filled[xx] = 1;
                if (color && !colored[xx]) {                  /* transparent sprites do not hide others */
                    colored[xx] = 1;
                    line[xx] = color;
                }
            }
        }
    }
}

void vdp_render_line(vdp9918 *v, int y, uint32_t *dst, int sprite_limit)
{
    uint8_t line[VDP_WIDTH];
    uint8_t bd = (uint8_t)(v->reg[7] & 15);
    uint32_t bdc = vdp9918_palette[bd];
    int x;

    if (!(v->reg[1] & 0x40u)) {                       /* display blanked */
        for (x = 0; x < VDP_WIDTH; x++) dst[x] = bdc;
        return;
    }
    memset(line, 0, sizeof line);
    render_background(v, y, line);
    if (!(v->reg[1] & 0x10u))                         /* no sprites in text mode */
        render_sprites(v, y, line, sprite_limit >= 32 ? 32 : 4);

    for (x = 0; x < VDP_WIDTH; x++) {
        uint8_t c = (uint8_t)(line[x] & 0x0Fu);
        dst[x] = c ? vdp9918_palette[c] : bdc;
    }
}
