/* TMS9918A video display processor (NTSC), scanline renderer. */
#ifndef VDP9918_H
#define VDP9918_H

#include <stdint.h>

#define VDP_WIDTH  256
#define VDP_HEIGHT 192

typedef struct vdp9918 {
    uint8_t  vram[0x4000];
    uint8_t  reg[8];
    uint8_t  status;
    uint8_t  latch;       /* 0: next control byte is the first, 1: the second */
    uint8_t  latch_data;
    uint8_t  rdbuf;
    uint16_t addr;
    uint8_t  write_mode;  /* informational */
} vdp9918;

extern const uint32_t vdp9918_palette[16];

void    vdp_reset(vdp9918 *v);
void    vdp_write_data(vdp9918 *v, uint8_t d);
void    vdp_write_ctrl(vdp9918 *v, uint8_t d);
uint8_t vdp_read_data(vdp9918 *v);
uint8_t vdp_read_status(vdp9918 *v);
int     vdp_int_line(const vdp9918 *v);          /* 1 while /INT is asserted */
void    vdp_set_frame_flag(vdp9918 *v);          /* call at start of vertical blank */

/* Render scanline y (0..191) into dst[256] as XRGB8888.
 * sprite_limit: 4 = hardware behaviour (5th sprite flag), otherwise up to 32 shown. */
void    vdp_render_line(vdp9918 *v, int y, uint32_t *dst, int sprite_limit);

#endif
