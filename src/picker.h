/* Cartridge-type picker: a small on-screen menu shown when the core cannot tell how a ROM image
 * is meant to be mapped (unlisted multi-bank dumps, odd sizes, unsupported RPK types).
 * Pure logic + software rendering over the 256x192 frame, like the on-screen keyboard.
 * The entries are the LCT_* values of loader.h (the first entry, LCT_AUTO, keeps the detected type).
 */
#ifndef PICKER_H
#define PICKER_H

#include <stdint.h>

#define PICKER_UP     0x01u
#define PICKER_DOWN   0x02u
#define PICKER_OK     0x04u      /* choose the highlighted type */
#define PICKER_CANCEL 0x08u      /* keep what is running */

#define PICKER_NONE   (-1)       /* picker still open / closed, nothing chosen this frame */
#define PICKER_CANCELLED (-2)

typedef struct picker {
    uint8_t  open;
    int8_t   sel;                /* index into the list (= LCT_* value) */
    int8_t   current;            /* LCT_* currently in use (marked with '>') */
    uint16_t prev;
    uint8_t  rep[2];
} picker;

void picker_init(picker *p);
/* current = LCT_* in effect (the cursor starts there). */
void picker_open(picker *p, int current);
int  picker_is_open(const picker *p);

/* Advance one frame with PICKER_* bits.  Returns the chosen LCT_* (>= 0), PICKER_CANCELLED, or PICKER_NONE. */
int  picker_frame(picker *p, unsigned buttons);

/* Draw over fb (256 x 192 XRGB8888).  name = file name, title = program names from the header (may be empty),
 * size_bytes = ROM size, detected = LCT_* the detection chose. */
void picker_draw(const picker *p, uint32_t *fb, const char *name, const char *title,
                 unsigned size_bytes, int header_ok, int detected);

#endif
