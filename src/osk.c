/* On-screen keyboard -- see osk.h */
#include <string.h>
#include "osk.h"
#include "keymatrix.h"

enum { KT_NORMAL, KT_SHIFT, KT_CTRL, KT_FCTN, KT_CAPS };

typedef struct {
    const char *label;
    uint8_t key;        /* TI matrix code (unused for modifiers / CAPS) */
    uint8_t fctn;       /* key is a FCTN shortcut: hold FCTN with it */
    uint8_t kind;
    uint8_t w;          /* relative width */
} okey;

#define NK(l, k)        { l, k, 0, KT_NORMAL, 2 }
#define NKW(l, k, w)    { l, k, 0, KT_NORMAL, w }
#define FK(l, k, w)     { l, k, 1, KT_NORMAL, w }

static const okey row0[] = { NK("1", K_1), NK("2", K_2), NK("3", K_3), NK("4", K_4), NK("5", K_5), NK("6", K_6),
                             NK("7", K_7), NK("8", K_8), NK("9", K_9), NK("0", K_0), NK("=", K_EQ) };
static const okey row1[] = { NK("Q", K_Q), NK("W", K_W), NK("E", K_E), NK("R", K_R), NK("T", K_T), NK("Y", K_Y),
                             NK("U", K_U), NK("I", K_I), NK("O", K_O), NK("P", K_P), NK("/", K_SLASH) };
static const okey row2[] = { NK("A", K_A), NK("S", K_S), NK("D", K_D), NK("F", K_F), NK("G", K_G), NK("H", K_H),
                             NK("J", K_J), NK("K", K_K), NK("L", K_L), NK(";", K_SEMI), NKW("ENTER", K_ENTER, 4) };
static const okey row3[] = { { "SHIFT", 0, 0, KT_SHIFT, 3 },
                             NK("Z", K_Z), NK("X", K_X), NK("C", K_C), NK("V", K_V), NK("B", K_B), NK("N", K_N),
                             NK("M", K_M), NK(",", K_COMMA), NK(".", K_PERIOD) };
static const okey row4[] = { { "CAPS", 0, 0, KT_CAPS, 4 }, { "CTRL", 0, 0, KT_CTRL, 4 }, { "FCTN", 0, 0, KT_FCTN, 4 },
                             NKW("SPACE", K_SPACE, 10) };
/* arrows are drawn from the glyphs \x01 (left) \x02 (right) \x03 (up) \x04 (down) */
static const okey row5[] = { FK("QUIT", K_EQ, 3), FK("BACK", K_9, 3), FK("CLR", K_4, 2), FK("DEL", K_1, 2),
                             FK("INS", K_2, 2), FK("\x01", K_S, 2), FK("\x02", K_D, 2), FK("\x03", K_E, 2),
                             FK("\x04", K_X, 2) };

typedef struct { const okey *k; int n; } orow;
static const orow rows[OSK_MAX_ROWS] = {
    { row0, (int)(sizeof row0 / sizeof row0[0]) }, { row1, (int)(sizeof row1 / sizeof row1[0]) },
    { row2, (int)(sizeof row2 / sizeof row2[0]) }, { row3, (int)(sizeof row3 / sizeof row3[0]) },
    { row4, (int)(sizeof row4 / sizeof row4[0]) }, { row5, (int)(sizeof row5 / sizeof row5[0]) }
};

#define PANEL_X     4
#define PANEL_W     248
#define ROW_H       13
#define HINT_H      10
#define PANEL_PAD   3
#define PANEL_H     (PANEL_PAD + OSK_MAX_ROWS * ROW_H + HINT_H + PANEL_PAD)
#define MIN_HOLD    4          /* frames a key stays down even for a quick tap (the console scans once per frame) */
#define REP_FIRST   18
#define REP_NEXT    5

int osk_row_count(void) { return OSK_MAX_ROWS; }
int osk_col_count(int row) { return (row >= 0 && row < OSK_MAX_ROWS) ? rows[row].n : 0; }
const char *osk_label(int row, int col)
{
    if (row < 0 || row >= OSK_MAX_ROWS || col < 0 || col >= rows[row].n) return "";
    return rows[row].k[col].label;
}

/* horizontal extent of a key in panel pixels */
static void key_span(int row, int col, int *x0, int *x1)
{
    int total = 0, before = 0, i;
    for (i = 0; i < rows[row].n; i++) {
        if (i < col) before += rows[row].k[i].w;
        total += rows[row].k[i].w;
    }
    *x0 = PANEL_X + before * PANEL_W / total;
    *x1 = PANEL_X + (before + rows[row].k[col].w) * PANEL_W / total;
}

static int key_center(int row, int col)
{
    int a, b;
    key_span(row, col, &a, &b);
    return (a + b) / 2;
}

void osk_init(osk *o)
{
    memset(o, 0, sizeof *o);
    o->pressed_row = o->pressed_col = -1;
    o->row = 2;
    o->col = 0;
    o->xhint = (int16_t)key_center(o->row, o->col);
}

int osk_is_open(const osk *o) { return o->open; }

static void release_all(osk *o)
{
    o->pressed_row = o->pressed_col = -1;
    o->hold_frames = 0;
    o->held_input = 0;
    o->mods[0] = o->mods[1] = o->mods[2] = 0;
    memset(o->rep_timer, 0, sizeof o->rep_timer);
}

static void nearest_in_row(osk *o, int row)
{
    int best = 0, bd = 1 << 30, i;
    for (i = 0; i < rows[row].n; i++) {
        int d = key_center(row, i) - o->xhint;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = i; }
    }
    o->row = (uint8_t)row;
    o->col = (uint8_t)best;
}

static void move_cursor(osk *o, unsigned dir)
{
    if (dir == OSK_LEFT || dir == OSK_RIGHT) {
        int n = rows[o->row].n, c = o->col;
        c += (dir == OSK_RIGHT) ? 1 : -1;
        if (c < 0) c = n - 1;
        if (c >= n) c = 0;
        o->col = (uint8_t)c;
        o->xhint = (int16_t)key_center(o->row, o->col);
    } else {
        int r = o->row + (dir == OSK_DOWN ? 1 : -1);
        if (r < 0) r = OSK_MAX_ROWS - 1;
        if (r >= OSK_MAX_ROWS) r = 0;
        nearest_in_row(o, r);                  /* keeps xhint so the column "sticks" while moving vertically */
    }
}

static void setkey(uint8_t keys[8], uint8_t k) { keys[k >> 3] |= (uint8_t)(1u << (k & 7u)); }

void osk_frame(osk *o, unsigned buttons, uint8_t keys[8], int *alpha_toggle)
{
    static const unsigned dirs[4] = { OSK_UP, OSK_DOWN, OSK_LEFT, OSK_RIGHT };
    unsigned rising = buttons & ~(unsigned)o->prev_buttons;
    int i;

    o->prev_buttons = (uint16_t)buttons;
    *alpha_toggle = 0;

    if (rising & OSK_TOGGLE) {
        o->open = (uint8_t)!o->open;
        release_all(o);
        return;
    }
    if (!o->open) return;
    if (rising & OSK_CLOSE) { o->open = 0; release_all(o); return; }
    if (rising & OSK_MOVE) o->at_top ^= 1u;

    for (i = 0; i < 4; i++) {
        if (buttons & dirs[i]) {
            if (rising & dirs[i]) { move_cursor(o, dirs[i]); o->rep_timer[i] = REP_FIRST; }
            else if (o->rep_timer[i] && --o->rep_timer[i] == 0) { move_cursor(o, dirs[i]); o->rep_timer[i] = REP_NEXT; }
        } else {
            o->rep_timer[i] = 0;
        }
    }

    if ((rising & OSK_PRESS) && o->pressed_row < 0) {
        const okey *k = &rows[o->row].k[o->col];
        switch (k->kind) {
        case KT_SHIFT: o->mods[0] = (uint8_t)((o->mods[0] + 1u) % 3u); break;
        case KT_CTRL:  o->mods[1] = (uint8_t)((o->mods[1] + 1u) % 3u); break;
        case KT_FCTN:  o->mods[2] = (uint8_t)((o->mods[2] + 1u) % 3u); break;
        case KT_CAPS:  *alpha_toggle = 1; break;
        default:
            o->pressed_row = (int8_t)o->row;
            o->pressed_col = (int8_t)o->col;
            o->hold_frames = 0;
            break;
        }
    }
    o->held_input = (buttons & OSK_PRESS) ? 1 : 0;

    if (o->pressed_row >= 0) {
        const okey *k = &rows[o->pressed_row].k[o->pressed_col];
        setkey(keys, k->key);
        if (k->fctn) setkey(keys, K_FCTN);
        if (o->hold_frames < 255) o->hold_frames++;
        if (!o->held_input && o->hold_frames >= MIN_HOLD) {      /* release: one-shot modifiers are used up */
            o->pressed_row = o->pressed_col = -1;
            for (i = 0; i < 3; i++) if (o->mods[i] == 1) o->mods[i] = 0;
        }
    }
    if (o->mods[0]) setkey(keys, K_SHIFT);
    if (o->mods[1]) setkey(keys, K_CTRL);
    if (o->mods[2]) setkey(keys, K_FCTN);
}

/* ------------------------------------------------------------- rendering */

/* classic 5x7 font, one byte per row, bit 4 = leftmost pixel */
static const uint8_t font_digit[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}
};
static const uint8_t font_alpha[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
};

static const uint8_t *glyph(char c)
{
    static const uint8_t blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t eq[7]    = { 0x00,0x00,0x1F,0x00,0x1F,0x00,0x00 };
    static const uint8_t slash[7] = { 0x01,0x01,0x02,0x04,0x08,0x10,0x10 };
    static const uint8_t semi[7]  = { 0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08 };
    static const uint8_t comma[7] = { 0x00,0x00,0x00,0x00,0x0C,0x04,0x08 };
    static const uint8_t dot[7]   = { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C };
    static const uint8_t colon[7] = { 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00 };
    static const uint8_t larr[7]  = { 0x00,0x04,0x08,0x1F,0x08,0x04,0x00 };
    static const uint8_t rarr[7]  = { 0x00,0x04,0x02,0x1F,0x02,0x04,0x00 };
    static const uint8_t uarr[7]  = { 0x04,0x0E,0x15,0x04,0x04,0x04,0x04 };
    static const uint8_t darr[7]  = { 0x04,0x04,0x04,0x04,0x15,0x0E,0x04 };
    if (c >= '0' && c <= '9') return font_digit[c - '0'];
    if (c >= 'A' && c <= 'Z') return font_alpha[c - 'A'];
    if (c >= 'a' && c <= 'z') return font_alpha[c - 'a'];
    switch (c) {
    case '=': return eq;   case '/': return slash; case ';': return semi;  case ',': return comma;
    case '.': return dot;  case ':': return colon;
    case '\x01': return larr; case '\x02': return rarr; case '\x03': return uarr; case '\x04': return darr;
    default: return blank;
    }
}

static void put_px(uint32_t *fb, int x, int y, uint32_t c)
{
    if (x >= 0 && x < OSK_W && y >= 0 && y < OSK_H_PX) fb[y * OSK_W + x] = c;
}

/* blend colour c over the pixel; alpha 0..256 */
static void blend_px(uint32_t *fb, int x, int y, uint32_t c, int a)
{
    uint32_t p;
    int r, g, b;
    if (x < 0 || x >= OSK_W || y < 0 || y >= OSK_H_PX) return;
    p = fb[y * OSK_W + x];
    r = (int)((p >> 16) & 255u) + (((int)((c >> 16) & 255u) - (int)((p >> 16) & 255u)) * a >> 8);
    g = (int)((p >> 8) & 255u)  + (((int)((c >> 8) & 255u)  - (int)((p >> 8) & 255u))  * a >> 8);
    b = (int)(p & 255u)         + (((int)(c & 255u)         - (int)(p & 255u))         * a >> 8);
    fb[y * OSK_W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fill_rect(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t c)
{
    int x, y;
    for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) put_px(fb, x, y, c);
}

static int text_width(const char *s) { int n = (int)strlen(s); return n ? n * 6 - 1 : 0; }

static void draw_text(uint32_t *fb, int x, int y, const char *s, uint32_t c)
{
    for (; *s; s++, x += 6) {
        const uint8_t *g = glyph(*s);
        int r, b;
        for (r = 0; r < 7; r++)
            for (b = 0; b < 5; b++)
                if (g[r] & (0x10u >> b)) put_px(fb, x + b, y + r, c);
    }
}

void osk_draw(const osk *o, uint32_t *fb, int alpha_lock)
{
    const int y0 = o->at_top ? 0 : OSK_H_PX - PANEL_H;
    int r, c, x, y;

    /* translucent panel: the game stays visible underneath */
    for (y = y0; y < y0 + PANEL_H; y++)
        for (x = 0; x < OSK_W; x++)
            blend_px(fb, x, y, 0x0A1030u, 200);

    for (r = 0; r < OSK_MAX_ROWS; r++) {
        for (c = 0; c < rows[r].n; c++) {
            const okey *k = &rows[r].k[c];
            int kx0, kx1, ky0 = y0 + PANEL_PAD + r * ROW_H, ky1 = ky0 + ROW_H - 1;
            uint32_t fill, text = 0xF0F0F0u;
            int sel = (o->open && r == o->row && c == o->col);
            int down = (r == o->pressed_row && c == o->pressed_col);
            key_span(r, c, &kx0, &kx1);
            kx1 -= 1;                                           /* 1 px gap between keys */
            if (k->kind != KT_NORMAL || k->fctn || k->w > 3) fill = 0x40306Eu; else fill = 0x2C3A70u;
            if (k->kind == KT_SHIFT && o->mods[0]) fill = o->mods[0] == 2 ? 0x2FAB5Bu : 0x2F78D8u;
            if (k->kind == KT_CTRL  && o->mods[1]) fill = o->mods[1] == 2 ? 0x2FAB5Bu : 0x2F78D8u;
            if (k->kind == KT_FCTN  && o->mods[2]) fill = o->mods[2] == 2 ? 0x2FAB5Bu : 0x2F78D8u;
            if (k->kind == KT_CAPS  && alpha_lock) fill = 0x2FAB5Bu;
            if (down) { fill = 0xE04A2Cu; text = 0xFFFFFFu; }
            if (sel)  { fill = down ? fill : 0xFFD23Fu; if (!down) text = 0x101010u; }
            fill_rect(fb, kx0, ky0, kx1, ky1, fill);
            if (sel) {                                          /* outline so the cursor is obvious even on gold keys */
                fill_rect(fb, kx0, ky0, kx1, ky0 + 1, 0xFFFFFFu);
                fill_rect(fb, kx0, ky1 - 1, kx1, ky1, 0xFFFFFFu);
                fill_rect(fb, kx0, ky0, kx0 + 1, ky1, 0xFFFFFFu);
                fill_rect(fb, kx1 - 1, ky0, kx1, ky1, 0xFFFFFFu);
            }
            draw_text(fb, kx0 + ((kx1 - kx0) - text_width(k->label)) / 2, ky0 + (ROW_H - 1 - 7) / 2, k->label, text);
        }
    }
    {
        static const char hint[] = "PAD:MOVE B:KEY A:CLOSE Y:POS";
        draw_text(fb, (OSK_W - text_width(hint)) / 2, y0 + PANEL_PAD + OSK_MAX_ROWS * ROW_H + 2, hint, 0xA0B0D0u);
    }
}
