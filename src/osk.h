/* Pop-up on-screen keyboard for the TI-99/4A.
 *
 * Pure logic + software rendering (no libretro dependency).  The front end feeds it the pad-1
 * buttons every frame; the keyboard returns TI key-matrix bits and draws itself over a copy of
 * the 256x192 frame.  Layout follows the real console: digits, QWERTY, ASDF row with ENTER,
 * SHIFT/ZXCV row, CAPS(alpha lock)/CTRL/FCTN/SPACE, plus a row of FCTN shortcuts.
 * SHIFT, CTRL and FCTN are sticky: press once = applies to the next key, press twice = locked,
 * a third time releases.
 */
#ifndef OSK_H
#define OSK_H

#include <stdint.h>

#define OSK_W 256
#define OSK_H_PX 192

/* input bits (the front end maps RetroPad buttons to these) */
#define OSK_UP     0x001u
#define OSK_DOWN   0x002u
#define OSK_LEFT   0x004u
#define OSK_RIGHT  0x008u
#define OSK_PRESS  0x010u      /* press the highlighted key (held = key held) */
#define OSK_CLOSE  0x020u
#define OSK_MOVE   0x040u      /* toggle top / bottom placement */
#define OSK_TOGGLE 0x080u      /* open / close (Select) */

#define OSK_MAX_ROWS 6
#define OSK_MAX_COLS 12

typedef struct osk {
    uint8_t  open;
    uint8_t  at_top;
    uint8_t  row, col;
    uint8_t  mods[3];            /* SHIFT, CTRL, FCTN: 0 off, 1 next key only, 2 locked */
    int16_t  xhint;              /* remembered horizontal centre for up/down moves */
    int8_t   pressed_row, pressed_col;   /* key currently held down, -1 = none */
    uint8_t  hold_frames;
    uint8_t  held_input;         /* PRESS still physically held */
    uint16_t prev_buttons;
    uint8_t  rep_timer[4];       /* auto-repeat for UP, DOWN, LEFT, RIGHT */
} osk;

void osk_init(osk *o);
int  osk_is_open(const osk *o);

/* Advance one frame.  'buttons' = OSK_* bits for this frame.  ORs the pressed keys into keys[8]
 * (TI matrix, columns 0-5).  *alpha_toggle is set to 1 if the CAPS key was pressed this frame, or to 2
 * if the CART key was pressed (the keyboard closes itself; the front end opens the cartridge picker). */
void osk_frame(osk *o, unsigned buttons, uint8_t keys[8], int *alpha_toggle);

/* Draw over fb (OSK_W x OSK_H_PX, XRGB8888).  alpha_lock lights the CAPS key. */
void osk_draw(const osk *o, uint32_t *fb, int alpha_lock);

/* Small drawing helpers (5x7 font, XRGB8888, OSK_W x OSK_H_PX) reused by the cartridge picker. */
int  osk_text_width(const char *s);
void osk_draw_text(uint32_t *fb, int x, int y, const char *s, uint32_t color);
void osk_fill_rect(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color);
void osk_shade_rect(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color, int alpha256);

/* Introspection for tests. */
int         osk_row_count(void);
int         osk_col_count(int row);
const char *osk_label(int row, int col);

#endif
