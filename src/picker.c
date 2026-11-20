/* Cartridge-type picker -- see picker.h */
#include <stdio.h>
#include <string.h>
#include "picker.h"
#include "osk.h"
#include "loader.h"

#define REP_FIRST 18
#define REP_NEXT  5
#define ROW_H     11
#define PANEL_X   6
#define PANEL_W   (OSK_W - 12)
#define HEAD_H    44

void picker_init(picker *p) { memset(p, 0, sizeof *p); }
int  picker_is_open(const picker *p) { return p->open; }

void picker_open(picker *p, int current)
{
    memset(p, 0, sizeof *p);
    p->open = 1;
    p->current = (int8_t)((current >= 0 && current < LCT_COUNT) ? current : LCT_AUTO);
    p->sel = p->current;
    p->prev = 0xFFFFu;           /* a button still held from before opening must not count as a press */
}

static void move(picker *p, int d)
{
    int s = p->sel + d;
    if (s < 0) s = LCT_COUNT - 1;
    if (s >= LCT_COUNT) s = 0;
    p->sel = (int8_t)s;
}

int picker_frame(picker *p, unsigned buttons)
{
    unsigned rising = buttons & ~(unsigned)p->prev;
    static const unsigned dirs[2] = { PICKER_UP, PICKER_DOWN };
    int i;

    p->prev = (uint16_t)buttons;
    if (!p->open) return PICKER_NONE;

    for (i = 0; i < 2; i++) {
        if (buttons & dirs[i]) {
            if (rising & dirs[i]) { move(p, i ? 1 : -1); p->rep[i] = REP_FIRST; }
            else if (p->rep[i] && --p->rep[i] == 0) { move(p, i ? 1 : -1); p->rep[i] = REP_NEXT; }
        } else {
            p->rep[i] = 0;
        }
    }
    if (rising & PICKER_CANCEL) { p->open = 0; return PICKER_CANCELLED; }
    if (rising & PICKER_OK) { p->open = 0; return p->sel; }
    return PICKER_NONE;
}

static void fit(char *dst, size_t n, const char *src, size_t maxc)
{
    size_t l = strlen(src);
    if (l <= maxc) { snprintf(dst, n, "%s", src); return; }
    snprintf(dst, n, "%.*s..", (int)(maxc - 2), src);
}

void picker_draw(const picker *p, uint32_t *fb, const char *name, const char *title,
                 unsigned size_bytes, int header_ok, int detected)
{
    const int y0 = 6, h = HEAD_H + LCT_COUNT * ROW_H + 16;
    char line[96], buf[96];
    int i;

    osk_shade_rect(fb, 0, 0, OSK_W, OSK_H_PX, 0x000000u, 120);
    osk_shade_rect(fb, PANEL_X, y0, PANEL_X + PANEL_W, y0 + h, 0x0A1030u, 235);
    osk_draw_text(fb, PANEL_X + 6, y0 + 5, "CARTRIDGE TYPE", 0xFFD23Fu);
    fit(buf, sizeof buf, (name && *name) ? name : "(UNNAMED)", 38);
    osk_draw_text(fb, PANEL_X + 6, y0 + 16, buf, 0xF0F0F0u);
    if (title && *title) { fit(buf, sizeof buf, title, 38); osk_draw_text(fb, PANEL_X + 6, y0 + 26, buf, 0x7FD0FFu); }
    else osk_draw_text(fb, PANEL_X + 6, y0 + 26, header_ok ? "" : "NO >AA HEADER AT >6000", 0xE08050u);
    snprintf(line, sizeof line, "%u BYTES   DETECTED: %s", size_bytes, loader_type_label(detected));
    fit(buf, sizeof buf, line, 40);
    osk_draw_text(fb, PANEL_X + 6, y0 + 36, buf, 0xA0B0D0u);

    for (i = 0; i < LCT_COUNT; i++) {
        int ry = y0 + HEAD_H + i * ROW_H;
        int sel = (i == p->sel);
        if (sel) osk_fill_rect(fb, PANEL_X + 3, ry - 1, PANEL_X + PANEL_W - 3, ry + ROW_H - 2, 0xFFD23Fu);
        snprintf(line, sizeof line, "%c %s", i == p->current ? '>' : ' ', loader_type_label(i));
        osk_draw_text(fb, PANEL_X + 8, ry + 1, line, sel ? 0x101010u : 0xF0F0F0u);
    }
    osk_draw_text(fb, PANEL_X + 6, y0 + HEAD_H + LCT_COUNT * ROW_H + 4, "UP/DOWN  B:SELECT  A:CANCEL", 0xA0B0D0u);
}
