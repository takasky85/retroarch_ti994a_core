/* Minimal read-only ZIP reader with a built-in DEFLATE decoder -- see zipread.h.
 * The inflate routine follows the structure of Mark Adler's public-domain "puff". */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "zipread.h"

/* ------------------------------------------------------------------ CRC32 */

uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static int ready = 0;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    if (!ready) {
        uint32_t n, k;
        for (n = 0; n < 256; n++) {
            uint32_t v = n;
            for (k = 0; k < 8; k++) v = (v & 1u) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
            table[n] = v;
        }
        ready = 1;
    }
    for (i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---------------------------------------------------------------- inflate */

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in; size_t inlen, incnt;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t outlen, outcnt;
} istate;

typedef struct { short count[MAXBITS + 1]; short symbol[FIXLCODES]; } huff;

static int ibits(istate *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static int idecode(istate *s, const huff *h)
{
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        int b = ibits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

static int iconstruct(huff *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int icodes(istate *s, const huff *lencode, const huff *distcode)
{
    static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int symbol;
    do {
        symbol = idecode(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            int len, dist, b;
            symbol -= 257;
            if (symbol >= 29) return -1;
            b = ibits(s, lext[symbol]); if (b < 0) return -1;
            len = lens[symbol] + b;
            symbol = idecode(s, distcode);
            if (symbol < 0 || symbol >= 30) return -1;
            b = ibits(s, dext[symbol]); if (b < 0) return -1;
            dist = dists[symbol] + b;
            if ((size_t)dist > s->outcnt || s->outcnt + (size_t)len > s->outlen) return -1;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - (size_t)dist]; s->outcnt++; }
        }
    } while (symbol != 256);
    return 0;
}

static int istored(istate *s)
{
    unsigned len, nlen;
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = (unsigned)(s->in[s->incnt] | (s->in[s->incnt + 1] << 8));
    nlen = (unsigned)(s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8));
    s->incnt += 4;
    if (len != (~nlen & 0xFFFFu)) return -1;
    if (s->incnt + len > s->inlen || s->outcnt + len > s->outlen) return -1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len; s->outcnt += len;
    return 0;
}

static int ifixed(istate *s)
{
    huff lencode, distcode;
    short lengths[FIXLCODES];
    int symbol;
    for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
    for (; symbol < 256; symbol++) lengths[symbol] = 9;
    for (; symbol < 280; symbol++) lengths[symbol] = 7;
    for (; symbol < FIXLCODES; symbol++) lengths[symbol] = 8;
    iconstruct(&lencode, lengths, FIXLCODES);
    for (symbol = 0; symbol < MAXDCODES; symbol++) lengths[symbol] = 5;
    iconstruct(&distcode, lengths, MAXDCODES);
    return icodes(s, &lencode, &distcode);
}

static int idynamic(istate *s)
{
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen, ndist, ncode, index, err;
    short lengths[MAXLCODES + MAXDCODES];
    huff lencode, distcode;

    nlen = ibits(s, 5); ndist = ibits(s, 5); ncode = ibits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    for (index = 0; index < ncode; index++) {
        int b = ibits(s, 3); if (b < 0) return -1;
        lengths[order[index]] = (short)b;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    err = iconstruct(&lencode, lengths, 19);
    if (err != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = idecode(s, &lencode), len, rep;
        if (symbol < 0) return -1;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                rep = ibits(s, 2); if (rep < 0) return -1; rep += 3;
            } else if (symbol == 17) {
                rep = ibits(s, 3); if (rep < 0) return -1; rep += 3;
            } else {
                rep = ibits(s, 7); if (rep < 0) return -1; rep += 11;
            }
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = iconstruct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -1;
    err = iconstruct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -1;
    return icodes(s, &lencode, &distcode);
}

int inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    istate s;
    int last, type, err = 0;
    s.in = in; s.inlen = in_len; s.incnt = 0; s.bitbuf = 0; s.bitcnt = 0;
    s.out = out; s.outlen = out_len; s.outcnt = 0;
    do {
        last = ibits(&s, 1);
        type = ibits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) err = istored(&s);
        else if (type == 1) err = ifixed(&s);
        else if (type == 2) err = idynamic(&s);
        else err = -1;
        if (err) return err;
    } while (!last);
    return s.outcnt == out_len ? 0 : -1;
}

/* -------------------------------------------------------------------- zip */

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int zip_looks_like_zip(const uint8_t *d, size_t n)
{
    return n >= 4 && d[0] == 'P' && d[1] == 'K' && (d[2] == 3 || d[2] == 5) && (d[3] == 4 || d[3] == 6);
}

int zip_open(zip_archive *z, const uint8_t *data, size_t size)
{
    size_t i, start, cd_off, n, p;
    int count, k;
    memset(z, 0, sizeof *z);
    if (size < 22) return -1;
    start = size > 22u + 65535u ? size - (22u + 65535u) : 0;
    i = size - 22u;
    for (;;) {
        if (le32(data + i) == 0x06054B50u) break;
        if (i == start) return -1;
        i--;
    }
    count = le16(data + i + 10);
    cd_off = le32(data + i + 16);
    if (cd_off >= size) return -1;
    z->entries = (zip_entry *)calloc((size_t)count ? (size_t)count : 1u, sizeof(zip_entry));
    if (!z->entries) return -1;
    p = cd_off;
    n = 0;
    for (k = 0; k < count; k++) {
        unsigned nl, el, cl;
        zip_entry *e = &z->entries[n];
        if (p + 46 > size || le32(data + p) != 0x02014B50u) break;
        e->method = le16(data + p + 10);
        e->crc = le32(data + p + 16);
        e->csize = le32(data + p + 20);
        e->usize = le32(data + p + 24);
        nl = le16(data + p + 28); el = le16(data + p + 30); cl = le16(data + p + 32);
        e->offset = le32(data + p + 42);
        if (p + 46 + nl > size) break;
        if (nl >= sizeof e->name) nl = (unsigned)sizeof e->name - 1u;
        memcpy(e->name, data + p + 46, nl);
        e->name[nl] = 0;
        p += 46u + le16(data + p + 28) + el + cl;
        if (nl && e->name[nl - 1] != '/') n++;        /* skip directories */
    }
    z->data = data; z->size = size; z->count = (int)n;
    return 0;
}

void zip_close(zip_archive *z)
{
    free(z->entries);
    memset(z, 0, sizeof *z);
}

static const char *base_name(const char *s)
{
    const char *b = s, *p;
    for (p = s; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int zip_find(const zip_archive *z, const char *name)
{
    int i;
    const char *want = base_name(name);
    for (i = 0; i < z->count; i++)
        if (ieq(base_name(z->entries[i].name), want)) return i;
    return -1;
}

uint8_t *zip_extract(const zip_archive *z, int index, size_t *out_size)
{
    const zip_entry *e;
    const uint8_t *lh, *src;
    size_t dpos;
    uint8_t *out;
    if (index < 0 || index >= z->count) return NULL;
    e = &z->entries[index];
    if ((size_t)e->offset + 30 > z->size) return NULL;
    lh = z->data + e->offset;
    if (le32(lh) != 0x04034B50u) return NULL;
    dpos = (size_t)e->offset + 30u + le16(lh + 26) + le16(lh + 28);
    if (dpos > z->size || (size_t)e->csize > z->size - dpos) return NULL;
    src = z->data + dpos;
    out = (uint8_t *)malloc((size_t)e->usize + 1u);
    if (!out) return NULL;
    if (e->method == 0) {
        if (e->csize != e->usize) { free(out); return NULL; }
        memcpy(out, src, e->usize);
    } else if (e->method == 8) {
        if (inflate_raw(src, e->csize, out, e->usize) != 0) { free(out); return NULL; }
    } else { free(out); return NULL; }
    if (crc32_calc(out, e->usize) != e->crc) { free(out); return NULL; }
    out[e->usize] = 0;
    if (out_size) *out_size = e->usize;
    return out;
}
