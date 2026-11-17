/* Minimal read-only ZIP reader with a built-in DEFLATE decoder (no zlib needed). */
#ifndef ZIPREAD_H
#define ZIPREAD_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char     name[256];
    uint32_t csize, usize, crc, offset;
    uint16_t method;
} zip_entry;

typedef struct {
    const uint8_t *data;
    size_t         size;
    zip_entry     *entries;
    int            count;
} zip_archive;

int  zip_looks_like_zip(const uint8_t *data, size_t size);
/* Returns 0 on success. 'data' must stay valid while the archive is in use. */
int  zip_open(zip_archive *z, const uint8_t *data, size_t size);
void zip_close(zip_archive *z);
/* Case-insensitive match on the file name (directories in the zip are ignored). */
int  zip_find(const zip_archive *z, const char *name);
/* Returns a malloc()ed buffer (caller frees) with *out_size bytes, or NULL. CRC is verified. */
uint8_t *zip_extract(const zip_archive *z, int index, size_t *out_size);

/* Raw DEFLATE; returns 0 on success. out_len must be the exact uncompressed size. */
int  inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
uint32_t crc32_calc(const uint8_t *data, size_t len);

#endif
