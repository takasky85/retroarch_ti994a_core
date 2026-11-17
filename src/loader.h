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

typedef struct {
    ti99_cart_desc desc;      /* valid after a successful load */
    uint8_t *rom;             /* owned */
    uint8_t *grom_data[8];    /* owned */
    char     note[256];       /* informational (e.g. which layout source was used) */
} loaded_cart;

void loaded_cart_free(loaded_cart *c);

/* Read a whole file (UTF-8 path on all platforms). Returns malloc()ed buffer with a trailing NUL. */
uint8_t *loader_read_file(const char *path, size_t *size);

/* rom: 8192 bytes, grom: 24576 bytes. Returns 0 on success, otherwise fills err. */
int loader_load_console(const char *sysdir, const char *content_dir,
                        uint8_t *rom, uint8_t *grom, char *err, size_t errsz);

/* path is only used for the cartridge name / extension. */
int loader_load_cart(const char *path, const uint8_t *data, size_t size,
                     const char *sysdir, loaded_cart *out, char *err, size_t errsz);

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
