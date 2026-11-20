/* BIOS and cartridge loading -- see loader.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "loader.h"
#include "zipread.h"
#include "softlist_data.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ------------------------------------------------------------------ files */

uint8_t *loader_read_file(const char *path, size_t *size)
{
    FILE *f;
    uint8_t *buf;
    long n;
#ifdef _WIN32
    wchar_t wpath[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024)) return NULL;
    f = _wfopen(wpath, L"rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)n + 1u);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    if (size) *size = (size_t)n;
    return buf;
}

static void seterr(char *err, size_t errsz, const char *fmt, const char *a, const char *b)
{
    if (err && errsz) snprintf(err, errsz, fmt, a ? a : "", b ? b : "");
}

static void lower(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

/* ---------------------------------------------------------------- console */

typedef struct { int idx; char key[256]; } sortref;

static int cmp_sortref(const void *a, const void *b) { return strcmp(((const sortref *)a)->key, ((const sortref *)b)->key); }

int loader_parse_console_zip(const uint8_t *zipdata, size_t size, uint8_t *rom, uint8_t *grom,
                             char *err, size_t errsz)
{
    zip_archive z;
    sortref cpu[8], gr[8];
    int ncpu = 0, ngr = 0, i, got_rom = 0, got_grom = 0;
    char listing[256] = "";

    if (zip_open(&z, zipdata, size) != 0) { seterr(err, errsz, "ti99_4a.zip is not a valid zip file", 0, 0); return -1; }
    memset(rom, 0, 0x2000);
    memset(grom, 0, 0x6000);

    for (i = 0; i < z.count; i++) {
        char n[256];
        const char *b = z.entries[i].name, *p;
        for (p = b; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
        strncpy(n, b, sizeof n - 1); n[sizeof n - 1] = 0;
        lower(n);
        if (strlen(listing) + strlen(n) + 3 < sizeof listing) { strcat(listing, n); strcat(listing, " "); }
        if (strstr(n, "grom") || strstr(n, "u500") || strstr(n, "u501") || strstr(n, "u502")) {
            if (ngr < 8) { gr[ngr].idx = i; strcpy(gr[ngr].key, n); ngr++; }
        } else if (strstr(n, "rom") || strstr(n, "u610") || strstr(n, "u611") || strstr(n, "ctg")) {
            if (ncpu < 8) { cpu[ncpu].idx = i; strcpy(cpu[ncpu].key, n); ncpu++; }
        }
    }
    qsort(cpu, (size_t)ncpu, sizeof cpu[0], cmp_sortref);
    qsort(gr, (size_t)ngr, sizeof gr[0], cmp_sortref);

    /* CPU ROM: one 8K file, or two 4K files (high byte / low byte) */
    for (i = 0; i < ncpu && !got_rom; i++) {
        if (z.entries[cpu[i].idx].usize == 0x2000u) {
            size_t n; uint8_t *d = zip_extract(&z, cpu[i].idx, &n);
            if (d) { memcpy(rom, d, 0x2000); free(d); got_rom = 1; }
        }
    }
    if (!got_rom) {
        int first = -1, second = -1, k;
        for (k = 0; k < ncpu; k++)
            if (z.entries[cpu[k].idx].usize == 0x1000u) { if (first < 0) first = cpu[k].idx; else if (second < 0) second = cpu[k].idx; }
        if (first >= 0 && second >= 0) {
            size_t n1, n2; uint8_t *hi = zip_extract(&z, first, &n1), *lo = zip_extract(&z, second, &n2);
            if (hi && lo) {
                unsigned j;
                for (j = 0; j < 0x1000u; j++) { rom[2 * j] = hi[j]; rom[2 * j + 1] = lo[j]; }
                got_rom = 1;
            }
            free(hi); free(lo);
        }
    }

    /* GROMs: one 24K file, or three 6K/8K chip dumps */
    for (i = 0; i < ngr && !got_grom; i++) {
        if (z.entries[gr[i].idx].usize == 0x6000u) {
            size_t n; uint8_t *d = zip_extract(&z, gr[i].idx, &n);
            if (d) { memcpy(grom, d, 0x6000); free(d); got_grom = 1; }
        }
    }
    if (!got_grom && ngr >= 3) {
        int k, ok = 1;
        for (k = 0; k < 3; k++) {
            unsigned sz = z.entries[gr[k].idx].usize;
            size_t n; uint8_t *d;
            if (sz != 0x1800u && sz != 0x2000u) { ok = 0; break; }
            d = zip_extract(&z, gr[k].idx, &n);
            if (!d) { ok = 0; break; }
            memcpy(grom + (size_t)k * 0x2000u, d, sz);
            free(d);
        }
        got_grom = ok;
    }

    zip_close(&z);
    if (!got_rom || !got_grom) {
        seterr(err, errsz, got_rom ? "ti99_4a.zip: console GROMs not recognised (found: %s)"
                                   : "ti99_4a.zip: console ROM not recognised (found: %s)", listing, 0);
        return -1;
    }
    return 0;
}

static uint8_t *find_and_read(const char *const *dirs, const char *file, char *used, size_t usedsz, size_t *size)
{
    char path[1024];
    int i;
    for (i = 0; dirs[i]; i++) {
        uint8_t *d;
        if (!dirs[i][0]) continue;
        snprintf(path, sizeof path, "%s/%s", dirs[i], file);
        d = loader_read_file(path, size);
        if (d) { if (used) { strncpy(used, path, usedsz - 1); used[usedsz - 1] = 0; } return d; }
    }
    return NULL;
}

int loader_load_console(const char *sysdir, const char *content_dir,
                        uint8_t *rom, uint8_t *grom, char *err, size_t errsz)
{
    char d1[1024], d2[1024], d3[1024], d4[1024];
    const char *dirs[8];
    uint8_t *z;
    size_t n;
    int rc;

    if (!sysdir) sysdir = "";
    snprintf(d1, sizeof d1, "%s", sysdir);
    snprintf(d2, sizeof d2, "%s/mame", sysdir);
    snprintf(d3, sizeof d3, "%s/ti99", sysdir);
    snprintf(d4, sizeof d4, "%s/mame/roms", sysdir);
    dirs[0] = d1; dirs[1] = d2; dirs[2] = d3; dirs[3] = d4;
    dirs[4] = content_dir ? content_dir : ""; dirs[5] = NULL;

    z = find_and_read(dirs, "ti99_4a.zip", NULL, 0, &n);
    if (!z) {
        seterr(err, errsz, "ti99_4a.zip (MAME console ROMs) not found in the system directory: %s", sysdir, 0);
        return -1;
    }
    rc = loader_parse_console_zip(z, n, rom, grom, err, errsz);
    free(z);
    return rc;
}

/* ------------------------------------------------------- speech synthesizer ROM */

/* MAME ti99_speech.zip: cd2325a.u2a (16K, >0000) + cd2326a.u2b (16K, >4000) */
#define SPEECH_CRC_U2A 0x1f58b571u
#define SPEECH_CRC_U2B 0x65d00401u

int loader_parse_speech_zip(const uint8_t *zipdata, size_t size, uint8_t *rom,
                            char *note, size_t notesz, char *err, size_t errsz)
{
    zip_archive z;
    sortref ref[8];
    int n = 0, i, got = 0, crc_ok = 0;
    char listing[256] = "";

    if (zip_open(&z, zipdata, size) != 0) { seterr(err, errsz, "ti99_speech.zip is not a valid zip file", 0, 0); return -1; }
    memset(rom, 0xFF, 0x8000);      /* unprogrammed ROM reads as ones -> stop frames */
    for (i = 0; i < z.count && n < 8; i++) {
        char nm[256];
        const char *b = z.entries[i].name, *p;
        for (p = b; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
        strncpy(nm, b, sizeof nm - 1); nm[sizeof nm - 1] = 0;
        lower(nm);
        if (strlen(listing) + strlen(nm) + 2 < sizeof listing) { strcat(listing, nm); strcat(listing, " "); }
        if (z.entries[i].usize == 0x4000u || z.entries[i].usize == 0x8000u) {
            ref[n].idx = i; strcpy(ref[n].key, nm); n++;
        }
    }
    qsort(ref, (size_t)n, sizeof ref[0], cmp_sortref);        /* cd2325a.u2a sorts before cd2326a.u2b */
    for (i = 0; i < n && !got; i++) {
        if (z.entries[ref[i].idx].usize == 0x8000u) {
            size_t dn; uint8_t *d = zip_extract(&z, ref[i].idx, &dn);
            if (d) { memcpy(rom, d, 0x8000); free(d); got = 2; crc_ok = 0; }
        }
    }
    if (!got && n >= 2) {
        size_t n1, n2;
        uint8_t *a = zip_extract(&z, ref[0].idx, &n1), *c = zip_extract(&z, ref[1].idx, &n2);
        if (a && c && n1 == 0x4000u && n2 == 0x4000u) {
            memcpy(rom, a, 0x4000); memcpy(rom + 0x4000, c, 0x4000);
            got = 2;
            crc_ok = (crc32_calc(a, n1) == SPEECH_CRC_U2A && crc32_calc(c, n2) == SPEECH_CRC_U2B);
        }
        free(a); free(c);
    }
    zip_close(&z);
    if (!got) { seterr(err, errsz, "ti99_speech.zip: expected cd2325a.u2a + cd2326a.u2b (found: %s)", listing, 0); return -1; }
    if (note && notesz)
        snprintf(note, notesz, crc_ok ? "speech ROM verified (MAME ti99_speech)" : "speech ROM loaded, but its CRC differs from MAME's dump");
    return 0;
}

int loader_load_speech(const char *sysdir, const char *content_dir, uint8_t *rom,
                       char *note, size_t notesz, char *err, size_t errsz)
{
    char d1[1024], d2[1024], d3[1024], d4[1024];
    const char *dirs[8];
    uint8_t *z;
    size_t n;
    int rc;

    if (!sysdir) sysdir = "";
    snprintf(d1, sizeof d1, "%s", sysdir);
    snprintf(d2, sizeof d2, "%s/mame", sysdir);
    snprintf(d3, sizeof d3, "%s/ti99", sysdir);
    snprintf(d4, sizeof d4, "%s/mame/roms", sysdir);
    dirs[0] = d1; dirs[1] = d2; dirs[2] = d3; dirs[3] = d4;
    dirs[4] = content_dir ? content_dir : ""; dirs[5] = NULL;
    z = find_and_read(dirs, "ti99_speech.zip", NULL, 0, &n);
    if (!z) { seterr(err, errsz, "ti99_speech.zip not found in the system directory: %s", sysdir, 0); return 1; }
    rc = loader_parse_speech_zip(z, n, rom, note, notesz, err, errsz);
    free(z);
    return rc ? -1 : 0;
}

/* ------------------------------------------------------------ tiny XML */

/* Read attribute 'key' from the tag text [t, te). */
static int xml_attr(const char *t, const char *te, const char *key, char *out, size_t outsz)
{
    size_t kl = strlen(key);
    const char *p;
    for (p = t; p + kl + 2 < te; p++) {
        if (!isspace((unsigned char)p[-1]) && p != t) continue;
        if (strncmp(p, key, kl) != 0 || p[kl] != '=') continue;
        {
            char q = p[kl + 1];
            const char *v, *e;
            if (q != '"' && q != '\'') continue;
            v = p + kl + 2;
            e = v;
            while (e < te && *e != q) e++;
            if (e >= te) return 0;
            if ((size_t)(e - v) >= outsz) return 0;
            memcpy(out, v, (size_t)(e - v));
            out[e - v] = 0;
            return 1;
        }
    }
    return 0;
}

/* Iterate tags in [p, end). Returns pointer past '>' or NULL. */
static const char *xml_next(const char *p, const char *end, const char **name, size_t *nlen,
                            const char **tend, int *closing)
{
    while (p < end) {
        const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
        const char *gt, *q;
        if (!lt) return NULL;
        if (lt + 4 <= end && strncmp(lt, "<!--", 4) == 0) {
            const char *e = strstr(lt + 4, "-->");
            if (!e || e >= end) return NULL;
            p = e + 3;
            continue;
        }
        gt = (const char *)memchr(lt, '>', (size_t)(end - lt));
        if (!gt) return NULL;
        if (lt[1] == '?' || lt[1] == '!') { p = gt + 1; continue; }
        q = lt + 1;
        *closing = 0;
        if (*q == '/') { *closing = 1; q++; }
        *name = q;
        while (q < gt && !isspace((unsigned char)*q) && *q != '/' && *q != '>') q++;
        *nlen = (size_t)(q - *name);
        *tend = gt;
        return gt + 1;
    }
    return NULL;
}

static int name_is(const char *n, size_t nl, const char *s) { return strlen(s) == nl && strncmp(n, s, nl) == 0; }

static uint32_t parse_num(const char *s)
{
    return (uint32_t)strtoul(s, NULL, 0);     /* handles 0x... and decimal */
}

/* --------------------------------------------------------- cart builder */

enum { K_ROM1, K_ROM2, K_GROM, K_RAM };

typedef struct {
    int      type;
    uint8_t *rom1, *rom2;
    uint32_t rom1_size, rom2_size;
    uint8_t *grom[8];
    uint32_t grom_addr[8], grom_size[8];
    int      ngrom;
    uint32_t ram_size;
    char     unsupported[64];
} builder;

static int grow(uint8_t **buf, uint32_t *cur, uint32_t need)
{
    if (need <= *cur) return 0;
    {
        uint8_t *nb = (uint8_t *)realloc(*buf, need ? need : 1u);
        if (!nb) return -1;
        memset(nb + *cur, 0, need - *cur);
        *buf = nb; *cur = need;
    }
    return 0;
}

static int bld_reserve(builder *b, int kind, uint32_t size)
{
    if (kind == K_ROM1) return grow(&b->rom1, &b->rom1_size, size);
    if (kind == K_ROM2) return grow(&b->rom2, &b->rom2_size, size);
    if (kind == K_RAM) { if (size > b->ram_size) b->ram_size = size; }
    return 0;
}

static int bld_add(builder *b, int kind, int idx, const uint8_t *data, size_t size, uint32_t offset)
{
    if (kind == K_ROM1) {
        if (grow(&b->rom1, &b->rom1_size, offset + (uint32_t)size)) return -1;
        memcpy(b->rom1 + offset, data, size);
    } else if (kind == K_ROM2) {
        if (grow(&b->rom2, &b->rom2_size, offset + (uint32_t)size)) return -1;
        memcpy(b->rom2 + offset, data, size);
    } else if (kind == K_GROM) {
        uint32_t addr = 0x6000u + 0x2000u * (uint32_t)(idx > 0 ? idx - 1 : 0) + offset;
        if (b->ngrom >= 8 || addr + size > 0x10000u) return -1;
        b->grom[b->ngrom] = (uint8_t *)malloc(size ? size : 1u);
        if (!b->grom[b->ngrom]) return -1;
        memcpy(b->grom[b->ngrom], data, size);
        b->grom_addr[b->ngrom] = addr;
        b->grom_size[b->ngrom] = (uint32_t)size;
        b->ngrom++;
    }
    return 0;
}

static void bld_free(builder *b)
{
    int i;
    free(b->rom1); free(b->rom2);
    for (i = 0; i < b->ngrom; i++) free(b->grom[i]);
    memset(b, 0, sizeof *b);
}

static int type_from_pcb(const char *s, builder *b)
{
    char t[64];
    snprintf(t, sizeof t, "%s", s);
    lower(t);
    if (!strcmp(t, "standard")) return CART_STANDARD;
    /* gromemu: GROM + banked ROM (e.g. a 64K ROM with a GROM that starts it); same bank select as "paged" ROMs */
    if (!strcmp(t, "gromemu")) return CART_PAGED378;
    if (!strcmp(t, "paged12k")) return CART_PAGED12K;
    if (!strcmp(t, "paged16k") || !strcmp(t, "paged")) return CART_PAGED16K;
    if (!strcmp(t, "paged379i")) return CART_PAGED379I;
    if (!strcmp(t, "paged378")) return CART_PAGED378;
    if (!strcmp(t, "paged377")) return CART_PAGED377;
    if (!strcmp(t, "mbx")) return CART_MBX;
    if (!strcmp(t, "minimem")) return CART_MINIMEM;
    /* paged7, super, ...: not emulated exactly; run it as a banked ROM and let the user pick another type */
    snprintf(b->unsupported, sizeof b->unsupported, "%s", t);
    return CART_PAGED378;
}

static int kind_from_area(const char *name, int *gidx)
{
    char n[64];
    const char *g;
    snprintf(n, sizeof n, "%s", name);
    lower(n);
    *gidx = 1;
    if ((g = strstr(n, "grom")) != NULL) {
        const char *d = g + 4;
        while (*d && !isdigit((unsigned char)*d) && *d != '_') d++;
        if (isdigit((unsigned char)*d)) *gidx = atoi(d);
        return K_GROM;
    }
    if (strstr(n, "ram")) return K_RAM;
    if (strstr(n, "rom2")) return K_ROM2;
    return K_ROM1;
}

static int finish(builder *b, loaded_cart *out, char *err, size_t errsz)
{
    int i;
    uint32_t total = b->rom1_size + b->rom2_size;
    if (!b->type) b->type = CART_STANDARD;
    if (total) {
        out->rom = (uint8_t *)malloc(total);
        if (!out->rom) return -1;
        if (b->rom1_size) memcpy(out->rom, b->rom1, b->rom1_size);
        if (b->rom2_size) memcpy(out->rom + b->rom1_size, b->rom2, b->rom2_size);
    }
    memset(&out->desc, 0, sizeof out->desc);
    out->desc.type = b->type;
    out->desc.rom = out->rom;
    out->desc.rom_size = total;
    out->desc.ram_size = b->ram_size;
    if (b->type == CART_MINIMEM && !out->desc.ram_size) out->desc.ram_size = 0x1000;
    if (b->type == CART_MBX && !out->desc.ram_size) out->desc.ram_size = 0x400;
    for (i = 0; i < b->ngrom; i++) {
        out->grom_data[i] = b->grom[i];
        b->grom[i] = NULL;
        out->desc.grom[i].addr = b->grom_addr[i];
        out->desc.grom[i].data = out->grom_data[i];
        out->desc.grom[i].size = b->grom_size[i];
    }
    out->desc.ngrom = b->ngrom;
    if (!total && !b->ngrom) { seterr(err, errsz, "cartridge contains no ROM or GROM data", 0, 0); return -1; }
    return 0;
}

/* ----------------------------------------------------- softlist / layout */

static int load_from_softlist(const char *xml, const char *name, const zip_archive *z, builder *b,
                              char *err, size_t errsz)
{
    char key[160];
    const char *blk, *blk_end, *p, *tn, *te;
    size_t nl;
    int closing, cur_kind = -1, cur_gidx = 1, in_part = 0, part_done = 0;
    char val[256], file[256];

    snprintf(key, sizeof key, "<software name=\"%s\"", name);
    blk = strstr(xml, key);
    if (!blk) return 1;                                   /* not in list */
    blk_end = strstr(blk, "</software>");
    if (!blk_end) return 1;

    p = blk;
    while ((p = xml_next(p, blk_end, &tn, &nl, &te, &closing)) != NULL) {
        const char *attrs = tn + nl;
        if (closing) {
            if (name_is(tn, nl, "dataarea")) cur_kind = -1;
            if (name_is(tn, nl, "part") && in_part) { in_part = 0; part_done = 1; }
            continue;
        }
        if (part_done) break;
        if (name_is(tn, nl, "part")) {
            if (xml_attr(attrs, te, "interface", val, sizeof val) && strcmp(val, "ti99_cart") != 0) continue;
            in_part = 1;
        } else if (!in_part) {
            continue;
        } else if (name_is(tn, nl, "feature")) {
            char fn[64];
            if (xml_attr(attrs, te, "name", fn, sizeof fn) && !strcmp(fn, "pcb") &&
                xml_attr(attrs, te, "value", val, sizeof val))
                b->type = type_from_pcb(val, b);
        } else if (name_is(tn, nl, "dataarea")) {
            char an[64];
            if (xml_attr(attrs, te, "name", an, sizeof an)) {
                cur_kind = kind_from_area(an, &cur_gidx);
                if (xml_attr(attrs, te, "size", val, sizeof val) && cur_kind != K_GROM)
                    bld_reserve(b, cur_kind, parse_num(val));
            }
            if (te > attrs && te[-1] == '/') cur_kind = -1;   /* empty element */
        } else if (name_is(tn, nl, "rom") && cur_kind >= 0) {
            uint32_t off = 0;
            int zi;
            uint8_t *d;
            size_t dn;
            if (!xml_attr(attrs, te, "name", file, sizeof file) || !file[0]) continue;
            if (xml_attr(attrs, te, "offset", val, sizeof val)) off = parse_num(val);
            zi = zip_find(z, file);
            if (zi < 0) { seterr(err, errsz, "cartridge zip is missing '%s'", file, 0); return -1; }
            d = zip_extract(z, zi, &dn);
            if (!d) { seterr(err, errsz, "cannot extract '%s' (corrupt zip?)", file, 0); return -1; }
            if (bld_add(b, cur_kind, cur_gidx, d, dn, off)) { free(d); seterr(err, errsz, "out of memory / bad layout for '%s'", file, 0); return -1; }
            free(d);
        }
    }
    return 0;
}

static int load_from_rpk(const zip_archive *z, builder *b, char *err, size_t errsz)
{
    int li = zip_find(z, "layout.xml");
    size_t n;
    uint8_t *xml = zip_extract(z, li, &n);
    const char *p, *tn, *te, *end;
    size_t nl;
    int closing;
    char ids[16][64], files[16][128];
    int nres = 0, rc = 0, i;
    char val[256], sid[64], uses[64];

    if (!xml) { seterr(err, errsz, "cannot read layout.xml from RPK", 0, 0); return -1; }
    end = (const char *)xml + n;

    /* pass 1: resources */
    p = (const char *)xml;
    while ((p = xml_next(p, end, &tn, &nl, &te, &closing)) != NULL) {
        const char *attrs = tn + nl;
        if (closing) continue;
        if (name_is(tn, nl, "rom") && nres < 16) {
            if (xml_attr(attrs, te, "id", ids[nres], sizeof ids[0]) &&
                xml_attr(attrs, te, "file", files[nres], sizeof files[0])) nres++;
        } else if (name_is(tn, nl, "pcb")) {
            if (xml_attr(attrs, te, "type", val, sizeof val)) b->type = type_from_pcb(val, b);
        }
    }
    /* pass 2: sockets */
    p = (const char *)xml;
    while ((p = xml_next(p, end, &tn, &nl, &te, &closing)) != NULL) {
        const char *attrs = tn + nl;
        if (closing || !name_is(tn, nl, "socket")) continue;
        if (xml_attr(attrs, te, "id", sid, sizeof sid) && xml_attr(attrs, te, "uses", uses, sizeof uses)) {
            int gidx, kind = kind_from_area(sid, &gidx);
            for (i = 0; i < nres; i++) {
                if (!strcmp(ids[i], uses)) {
                    int zi = zip_find(z, files[i]);
                    size_t dn;
                    uint8_t *d;
                    if (zi < 0) { seterr(err, errsz, "RPK is missing '%s'", files[i], 0); rc = -1; break; }
                    d = zip_extract(z, zi, &dn);
                    if (!d) { seterr(err, errsz, "cannot extract '%s'", files[i], 0); rc = -1; break; }
                    if (kind != K_RAM && bld_add(b, kind, gidx, d, dn, 0)) rc = -1;
                    free(d);
                    break;
                }
            }
            if (kind == K_RAM) {
                /* RAM sockets refer to <ram id=... length=...>; size is only needed for save RAM */
                if (b->type == CART_MINIMEM) b->ram_size = 0x1000; else if (b->type == CART_MBX) b->ram_size = 0x400;
            }
        }
        if (rc) break;
    }
    free(xml);
    return rc;
}

/* ------------------------------------------------------------- heuristic */

/* Files in a cartridge zip that are not ROM images (readmes, pictures, ...). */
static int is_junk_name(const char *name)
{
    static const char *const junk[] = { "txt", "md", "nfo", "diz", "htm", "html", "pdf", "png", "jpg", "jpeg",
                                        "gif", "bmp", "doc", "rtf", "ini", "cfg", "xml", "url", "zip", "7z", NULL };
    char n[128];
    const char *dot;
    int i;
    snprintf(n, sizeof n, "%s", name);
    lower(n);
    dot = strrchr(n, '.');
    if (!dot) return 0;
    for (i = 0; junk[i]; i++) if (!strcmp(dot + 1, junk[i])) return 1;
    return 0;
}

/* multi = the zip holds several files, so a trailing c/d/g letter is a MAME-style role suffix */
static int load_heuristic(const char *fname, const uint8_t *data, size_t size, builder *b, int multi)
{
    char base[128], ext[16];
    size_t l;
    const char *dot;
    int kind = K_ROM1, gidx = 1;
    snprintf(base, sizeof base, "%s", fname);
    lower(base);
    dot = strrchr(base, '.');
    ext[0] = 0;
    if (dot) snprintf(ext, sizeof ext, "%s", dot + 1);
    l = dot ? (size_t)(dot - base) : strlen(base);
    if (l >= 2 && base[l - 2] == 'g' && base[l - 1] >= '3' && base[l - 1] <= '9') {
        /* TI naming: phm3056g3.bin is the 6K GROM at >6000, g4 at >8000, g5 at >A000, g6 at >C000 */
        kind = K_GROM;
        gidx = base[l - 1] - '2';
    } else if (!strcmp(ext, "g")) kind = K_GROM;
    else if (!strcmp(ext, "d")) kind = K_ROM2;
    else if (!strcmp(ext, "c")) kind = K_ROM1;
    else if (l >= 2 && (multi || isdigit((unsigned char)base[l - 2]))) {
        if (base[l - 1] == 'g') kind = K_GROM;
        else if (base[l - 1] == 'd') kind = K_ROM2;
    }
    return bld_add(b, kind, gidx, data, size, 0);
}

/* Pick a cartridge type for ROM files nobody described. Returns 1 when it is only a guess. */
static int guess_type(builder *b)
{
    if (b->rom2_size) { b->type = CART_PAGED16K; return 0; }            /* c.bin + d.bin */
    if (b->rom1_size > 0x4000u) { b->type = CART_PAGED378; return 1; }  /* 32K/64K/... homebrew, 8K banks */
    if (b->rom1_size > 0x2000u) { b->type = CART_PAGED16K; return 1; }  /* 12K/16K: two banks */
    b->type = CART_STANDARD;
    return 0;
}

/* Program names from the >AA header (program list at >6006) of the first ROM bank or the GROM at >6000. */
static void describe_header(loaded_cart *c)
{
    const uint8_t *img = NULL;
    uint32_t n = 0, ptr, hops;
    char *o = c->title;
    size_t left = sizeof c->title;
    int i;
    c->title[0] = 0;
    c->header_ok = 0;
    if (c->desc.rom_size >= 8 && c->desc.rom[0] == 0xAAu) { img = c->desc.rom; n = c->desc.rom_size; }
    else {
        for (i = 0; i < c->desc.ngrom; i++)
            if (c->desc.grom[i].addr == 0x6000u && c->desc.grom[i].size >= 8 && c->desc.grom[i].data[0] == 0xAAu) {
                img = c->desc.grom[i].data; n = c->desc.grom[i].size; break;
            }
    }
    if (!img) return;
    c->header_ok = 1;
    if (n > 0x2000u) n = 0x2000u;
    ptr = (uint32_t)((img[6] << 8) | img[7]);
    for (hops = 0; hops < 6 && ptr >= 0x6000u && ptr + 5u < 0x6000u + n; hops++) {
        uint32_t o0 = ptr - 0x6000u, len = img[o0 + 4], k;
        if (len == 0 || len > 30 || o0 + 5u + len > n) break;
        if (o != c->title && left > 3) { *o++ = ','; *o++ = ' '; left -= 2; }
        for (k = 0; k < len && left > 1; k++) {
            uint8_t ch = img[o0 + 5 + k];
            *o++ = (char)((ch >= 32 && ch < 127) ? ch : '?');
            left--;
        }
        *o = 0;
        ptr = (uint32_t)((img[o0] << 8) | img[o0 + 1]);
    }
}

static int lct_from_type(int type)
{
    switch (type) {
    case CART_PAGED12K: return LCT_XB12K;
    case CART_PAGED16K: return LCT_16K;
    case CART_PAGED378: case CART_PAGED377: return LCT_BANKED;
    case CART_PAGED379I: return LCT_BANKED_INV;
    case CART_MBX: return LCT_MBX;
    case CART_MINIMEM: return LCT_MINIMEM;
    default: return LCT_STANDARD;
    }
}

static int type_from_lct(int lct, int fallback)
{
    switch (lct) {
    case LCT_STANDARD: return CART_STANDARD;
    case LCT_16K: return CART_PAGED16K;
    case LCT_BANKED: return CART_PAGED378;
    case LCT_BANKED_INV: return CART_PAGED379I;
    case LCT_XB12K: return CART_PAGED12K;
    case LCT_MBX: return CART_MBX;
    case LCT_MINIMEM: return CART_MINIMEM;
    default: return fallback;
    }
}

static const struct { const char *option, *label; } lct_names[LCT_COUNT] = {
    { "auto",       "Automatic (detected)" },
    { "standard",   "Standard 8K ROM + GROMs" },
    { "16k",        "2 x 8K banks (16K)" },
    { "banked",     "Banked ROM, 8K banks" },
    { "banked_inv", "Banked ROM, inverted order" },
    { "xb12k",      "Extended BASIC 12K" },
    { "mbx",        "MBX (1K RAM + banks)" },
    { "minimem",    "Mini Memory (4K RAM)" }
};

const char *loader_type_label(int lct) { return (lct >= 0 && lct < LCT_COUNT) ? lct_names[lct].label : "?"; }
const char *loader_type_option(int lct) { return (lct >= 0 && lct < LCT_COUNT) ? lct_names[lct].option : "auto"; }
int loader_type_from_option(const char *v)
{
    int i;
    if (v) for (i = 0; i < LCT_COUNT; i++) if (!strcmp(v, lct_names[i].option)) return i;
    return LCT_AUTO;
}

int loader_apply_type(loaded_cart *c, int lct)
{
    int t = type_from_lct(lct == LCT_AUTO ? c->detected : lct, CART_STANDARD);
    c->desc.type = t;
    c->desc.ram_size = 0;
    if (t == CART_MINIMEM) c->desc.ram_size = 0x1000;
    if (t == CART_MBX) c->desc.ram_size = 0x400;
    return 0;
}

/* ----------------------------------------------------------------- public */

void loaded_cart_free(loaded_cart *c)
{
    int i;
    free(c->rom);
    for (i = 0; i < 8; i++) free(c->grom_data[i]);
    memset(c, 0, sizeof *c);
}

int loader_load_cart(const char *path, const uint8_t *data, size_t size, const char *sysdir,
                     loaded_cart *out, char *err, size_t errsz)
{
    return loader_load_cart_ex(path, data, size, sysdir, LCT_AUTO, out, err, errsz);
}

int loader_load_cart_ex(const char *path, const uint8_t *data, size_t size, const char *sysdir, int force,
                        loaded_cart *out, char *err, size_t errsz)
{
    builder b;
    int guessed = 0;
    char name[128], ext[16];
    const char *bn = path ? path : "", *p, *dot;
    int rc = -1;

    memset(out, 0, sizeof *out);
    memset(&b, 0, sizeof b);
    for (p = bn; *p; p++) if (*p == '/' || *p == '\\') bn = p + 1;
    snprintf(name, sizeof name, "%s", bn);
    dot = strrchr(name, '.');
    ext[0] = 0;
    if (dot) { snprintf(ext, sizeof ext, "%s", dot + 1); lower(ext); name[dot - name] = 0; }
    lower(name);

    if (zip_looks_like_zip(data, size)) {
        zip_archive z;
        if (zip_open(&z, data, size) != 0) { seterr(err, errsz, "cartridge zip is corrupt", 0, 0); return -1; }
        if (zip_find(&z, "layout.xml") >= 0) {
            rc = load_from_rpk(&z, &b, err, errsz);
            snprintf(out->note, sizeof out->note, "RPK layout");
        } else {
            char dirs[5][1024];
            const char *dl[6];
            char used[1100];
            uint8_t *xml;
            size_t xn;
            int i, had_xml;
            snprintf(dirs[0], sizeof dirs[0], "%s/mame/hash", sysdir ? sysdir : "");
            snprintf(dirs[1], sizeof dirs[1], "%s/hash", sysdir ? sysdir : "");
            snprintf(dirs[2], sizeof dirs[2], "%s/ti99/hash", sysdir ? sysdir : "");
            snprintf(dirs[3], sizeof dirs[3], "%s/ti99", sysdir ? sysdir : "");
            snprintf(dirs[4], sizeof dirs[4], "%s", sysdir ? sysdir : "");
            for (i = 0; i < 5; i++) dl[i] = dirs[i];
            dl[5] = NULL;
            xml = find_and_read(dl, "ti99_cart.xml", used, sizeof used, &xn);
            rc = 1;
            had_xml = (xml != NULL);
            if (xml) {
                rc = load_from_softlist((const char *)xml, name, &z, &b, err, errsz);
                if (rc == 0) snprintf(out->note, sizeof out->note, "softlist layout (%s)", name);
                free(xml);
            }
            if (rc == 1) {                                 /* not in an external list: built-in MAME list */
                bld_free(&b);
                memset(&b, 0, sizeof b);
                rc = load_from_softlist(softlist_xml, name, &z, &b, err, errsz);
                if (rc == 0) snprintf(out->note, sizeof out->note, "built-in MAME software list (%s)", name);
            }
            if (rc == 1) {                                 /* not listed anywhere */
                bld_free(&b);
                memset(&b, 0, sizeof b);
                rc = 0;
                {
                    int nfiles = 0;
                    for (i = 0; i < z.count; i++) if (!is_junk_name(z.entries[i].name)) nfiles++;
                    for (i = 0; i < z.count && rc == 0; i++) {
                        size_t dn;
                        uint8_t *d;
                        if (is_junk_name(z.entries[i].name)) continue;
                        d = zip_extract(&z, i, &dn);
                        if (d) { rc = load_heuristic(z.entries[i].name, d, dn, &b, nfiles > 1); free(d); }
                    }
                }
                guessed = guess_type(&b);
                snprintf(out->note, sizeof out->note, "guessed layout from file names (%s)",
                         had_xml ? "not in the software lists" : "not in the built-in software list");
                if (rc != 0) seterr(err, errsz, "could not read cartridge files", 0, 0);
            }
        }
        zip_close(&z);
    } else if (size) {
        rc = load_heuristic(bn, data, size, &b, 0);
        snprintf(out->note, sizeof out->note, "raw dump");
        if (rc == 0) guessed = guess_type(&b);
    } else {
        seterr(err, errsz, "empty content file", 0, 0);
    }

    if (rc == 0) {
        if (b.unsupported[0]) guessed = 1;
        rc = finish(&b, out, err, errsz);
    }
    if (rc == 0) {
        out->detected = lct_from_type(out->desc.type);
        describe_header(out);
        /* 0 = layout known, 1 = plausible guess (tell the user), 2 = really unsure (ask the user) */
        out->guessed = guessed ? 1 : 0;
        if (guessed && (!out->header_ok || b.unsupported[0] || out->desc.rom_size == 0x3000u)) out->guessed = 2;
        if (force != LCT_AUTO) loader_apply_type(out, force);
    }
    bld_free(&b);
    if (rc != 0) loaded_cart_free(out);
    return rc;
}
