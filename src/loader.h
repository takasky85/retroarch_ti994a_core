/* BIOS and cartridge loading using MAME's file formats:
 *   - console:  ti99_4a.zip (MAME BIOS set: per-chip dumps or the older combined files)
 *   - carts:    MAME software-list zips (layout from the built-in copy of MAME's ti99_cart.xml,
 *               or from a newer ti99_cart.xml in the system directory), RPK packages
 *               (layout.xml inside), or raw .bin/.c/.g/.d dumps.
 */
#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "ti99.h"

/* Cartridge types the user can pick (core option / on-screen picker). LCT_AUTO = use the detected one. */
enum { LCT_AUTO = 0, LCT_STANDARD, LCT_16K, LCT_BANKED, LCT_BANKED_INV, LCT_XB12K, LCT_MBX, LCT_MINIMEM, LCT_COUNT };

typedef struct {
    ti99_cart_desc desc;      /* valid after a successful load */
    uint8_t *rom;             /* owned */
    uint8_t *grom_data[8];    /* owned */
    char     note[256];       /* informational (e.g. which layout source was used) */
    int      guessed;         /* 0 = layout known, 1 = plausible guess, 2 = unsure (offer the type picker) */
    int      detected;        /* LCT_* chosen by detection (before any override) */
    int      header_ok;       /* ROM or GROM at >6000 starts with the >AA cartridge header */
    char     title[96];       /* program names found in the header, e.g. "TI INVADERS" */
} loaded_cart;

/* Human readable type names for menus / option values. */
const char *loader_type_label(int lct);        /* e.g. "2 x 8K banks (16K)" */
const char *loader_type_option(int lct);       /* core option value, e.g. "banked_inv" */
int         loader_type_from_option(const char *value);   /* -> LCT_*, LCT_AUTO when unknown */

void loaded_cart_free(loaded_cart *c);

/* Read a whole file (UTF-8 path on all platforms). Returns malloc()ed buffer with a trailing NUL. */
uint8_t *loader_read_file(const char *path, size_t *size);

/* rom: 8192 bytes, grom: 24576 bytes. Returns 0 on success, otherwise fills err. */
int loader_load_console(const char *sysdir, const char *content_dir,
                        uint8_t *rom, uint8_t *grom, char *err, size_t errsz);

/* path is only used for the cartridge name / extension. */
int loader_load_cart(const char *path, const uint8_t *data, size_t size,
                     const char *sysdir, loaded_cart *out, char *err, size_t errsz);

/* Same, but 'force' (LCT_*) overrides the detected cartridge type; LCT_AUTO keeps the detection. */
int loader_load_cart_ex(const char *path, const uint8_t *data, size_t size,
                        const char *sysdir, int force, loaded_cart *out, char *err, size_t errsz);

/* Re-apply a type to an already loaded cartridge (picker). Returns 0 on success. */
int loader_apply_type(loaded_cart *c, int lct);

/* Speech synthesizer vocabulary ROM (MAME ti99_speech.zip: cd2325a.u2a + cd2326a.u2b), 32768 bytes.
 * Returns 0 = loaded, 1 = zip not found (not an error: speech is simply unavailable),
 * -1 = found but unusable (err filled).  'note' receives a short status line. */
int loader_load_speech(const char *sysdir, const char *content_dir, uint8_t *rom,
                       char *note, size_t notesz, char *err, size_t errsz);

/* Exposed for tests. */
int loader_parse_speech_zip(const uint8_t *zipdata, size_t size, uint8_t *rom,
                            char *note, size_t notesz, char *err, size_t errsz);
int loader_parse_console_zip(const uint8_t *zipdata, size_t size, uint8_t *rom, uint8_t *grom,
                             char *err, size_t errsz);

#endif
