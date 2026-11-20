/* TI-99/4A console: memory map, TMS9901, GROMs, cartridges, frame loop. */
#ifndef TI99_H
#define TI99_H

#include <stdint.h>
#include <stddef.h>
#include "vdp9918.h"

#define TI99_FRAME_LINES      262
#define TI99_CYCLES_PER_LINE  191
#define TI99_CPU_CLOCK        3000000.0
#define TI99_FPS              (TI99_CPU_CLOCK / (double)(TI99_FRAME_LINES * TI99_CYCLES_PER_LINE))
#define TI99_MAX_AUDIO_FRAMES 1200

typedef enum {
    CART_NONE = 0,
    CART_STANDARD,      /* 0..8 KiB ROM (+ GROMs)                         */
    CART_PAGED12K,      /* Extended BASIC: 4K fixed + 2 x 4K banks        */
    CART_PAGED16K,      /* 2 x 8K banks (Atarisoft, old "paged")          */
    CART_PAGED379I,     /* up to 128K, inverted bank select               */
    CART_PAGED378,      /* up to 512K                                     */
    CART_PAGED377,      /* up to 2M                                       */
    CART_MBX,           /* 1K RAM at >6C00 + banked ROM (experimental)    */
    CART_MINIMEM        /* Mini Memory: 4K ROM + 4K battery RAM           */
} ti99_cart_type;

typedef struct { uint32_t addr; const uint8_t *data; uint32_t size; } ti99_blob;

/* Description of a cartridge; all data is copied by ti99_insert_cart(). */
typedef struct {
    int            type;
    const uint8_t *rom;       /* all ROM banks concatenated (socket 1, then socket 2) */
    uint32_t       rom_size;
    ti99_blob      grom[8];   /* addr = GROM address (>6000, >8000, ...) */
    int            ngrom;
    uint32_t       ram_size;  /* persistent cartridge RAM, if any */
} ti99_cart_desc;

typedef struct ti99 ti99;

ti99 *ti99_create(unsigned sample_rate);
void  ti99_destroy(ti99 *m);

/* rom: 8192 bytes (big-endian words), grom: 24576 bytes (three 8K windows). */
void  ti99_set_console(ti99 *m, const uint8_t *rom, const uint8_t *grom);
int   ti99_insert_cart(ti99 *m, const ti99_cart_desc *d);   /* 0 = ok */
void  ti99_remove_cart(ti99 *m);
void  ti99_set_options(ti99 *m, int expansion_32k, int sprite_limit);
void  ti99_set_sprite_limit(ti99 *m, int sprite_limit);

/* Speech synthesizer: rom = 32768 bytes (MAME ti99_speech: cd2325a.u2a + cd2326a.u2b), NULL removes it.
 * The synthesizer is active when a ROM is present and it is enabled (default: enabled). */
int   ti99_set_speech_rom(ti99 *m, const uint8_t *rom);
void  ti99_set_speech_enabled(ti99 *m, int enabled);
int   ti99_speech_active(const ti99 *m);
int   ti99_speech_talking(const ti99 *m);
void  ti99_reset(ti99 *m);

/* keys[col] bit row: TI keyboard matrix (cols 0-5), joystick 1 = col 6, joystick 2 = col 7.
 * Joystick rows: 0 fire, 1 left, 2 right, 3 down, 4 up. */
void  ti99_set_keys(ti99 *m, const uint8_t keys[8], int alphalock);

/* Run one video frame. 'audio' receives interleaved stereo; *nframes = sample frames written. */
void  ti99_run_frame(ti99 *m, int16_t *audio, int *nframes);
const uint32_t *ti99_framebuffer(const ti99 *m);

uint8_t *ti99_expansion_ram(ti99 *m, size_t *size);
uint8_t *ti99_cart_ram(ti99 *m, size_t *size);

size_t ti99_state_size(void);
int    ti99_save_state(const ti99 *m, void *buf, size_t size);
int    ti99_load_state(ti99 *m, const void *buf, size_t size);

/* Test hooks */
uint16_t ti99_peek16(ti99 *m, uint16_t addr);
void     ti99_poke_cart(ti99 *m, uint16_t addr, uint16_t v);   /* write into >6000-7FFF (bank switching) */
void     ti99_run_cycles(ti99 *m, int cycles);
void    *ti99_cpu(ti99 *m);
vdp9918 *ti99_vdp(ti99 *m);

#endif
