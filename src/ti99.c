/* TI-99/4A console -- see ti99.h */
#include <stdlib.h>
#include <string.h>
#include "ti99.h"
#include "cpu9900.h"
#include "psg76489.h"
#include "speech.h"

#define STATE_MAGIC 0x54493939u   /* 'TI99' */
#define STATE_VERSION 2u

struct ti99 {
    /* ------------------------------------------------ saved state */
    tms9900  cpu;
    vdp9918  vdp;
    psg76489 psg;
    uint8_t  pad[256];
    uint8_t  exp[0x8000];            /* >2000-3FFF and >A000-FFFF */
    uint8_t  cartram[0x1000];
    uint16_t grom_addr;
    uint8_t  grom_buf, grom_wt, grom_rt, grom_hi;
    uint8_t  cart_bank;
    /* TMS9901 */
    uint16_t pic_mask;
    uint8_t  timer_mode, timer_int, timer_run;
    uint16_t timer_load, timer_cnt, timer_latch;
    uint8_t  pout[16];
    uint32_t timer_acc;
    int32_t  budget;
    uint32_t aud_acc;
    uint8_t  expansion;
    speech   sp;                     /* SPEECH_SAVED_SIZE bytes are saved */
    /* ------------------------------------------------ not saved */
    uint8_t  speech_rom[SPEECH_ROM_SIZE];
    uint8_t  speech_has_rom, speech_enabled, speech_on;
    uint8_t  sprite_limit;
    uint8_t  keys[8];
    uint8_t  alphalock;
    uint8_t  rom[0x2000];
    uint8_t  grom[0x10000];
    int      cart_type;
    uint8_t *cart_rom;
    uint32_t cart_rom_size;
    uint32_t cart_ram_size;
    uint32_t aud_step;               /* samples per scanline, 16.16 */
    unsigned sample_rate;
    uint32_t fb[VDP_WIDTH * VDP_HEIGHT];
};

/* ----------------------------------------------------------------- carts */

static uint16_t rom16(const ti99 *m, uint32_t off)
{
    off %= m->cart_rom_size;
    return (uint16_t)((m->cart_rom[off] << 8) | m->cart_rom[(off + 1) % m->cart_rom_size]);
}

static uint32_t nbanks8k(const ti99 *m)
{
    uint32_t n = m->cart_rom_size / 0x2000u;
    return n ? n : 1u;
}

static uint16_t cart_read(const ti99 *m, uint16_t a)
{
    uint32_t off = (uint32_t)(a - 0x6000u);
    if (!m->cart_rom_size && m->cart_type != CART_MBX) return 0;
    switch (m->cart_type) {
    case CART_STANDARD:
        return rom16(m, m->cart_rom_size >= 0x2000u ? off : off % m->cart_rom_size);
    case CART_PAGED12K:
        if (off < 0x1000u) return rom16(m, off);
        return rom16(m, 0x1000u + (uint32_t)(m->cart_bank & 1u) * 0x1000u + (off - 0x1000u));
    case CART_PAGED16K: case CART_PAGED379I: case CART_PAGED378: case CART_PAGED377:
        return rom16(m, (uint32_t)m->cart_bank * 0x2000u + off);
    case CART_MBX:
        if (off >= 0x0C00u && off < 0x0FFEu)
            return (uint16_t)((m->cartram[off - 0x0C00u] << 8) | m->cartram[off - 0x0C00u + 1u]);
        return rom16(m, (uint32_t)m->cart_bank * 0x2000u + off);
    case CART_MINIMEM:
        if (off < 0x1000u) return rom16(m, off);
        return (uint16_t)((m->cartram[off - 0x1000u] << 8) | m->cartram[off - 0x1000u + 1u]);
    default:
        return 0;
    }
}

static void cart_write(ti99 *m, uint16_t a, uint16_t v)
{
    uint32_t off = (uint32_t)(a - 0x6000u);
    switch (m->cart_type) {
    case CART_PAGED12K: case CART_PAGED16K:
        m->cart_bank = (uint8_t)((off >> 1) & 1u);
        break;
    case CART_PAGED379I:
        m->cart_bank = (uint8_t)((~(off >> 1)) & (nbanks8k(m) - 1u));
        break;
    case CART_PAGED378: case CART_PAGED377:
        m->cart_bank = (uint8_t)((off >> 1) & (nbanks8k(m) - 1u));
        break;
    case CART_MBX:
        if (off >= 0x0C00u && off < 0x0FFEu) {
            m->cartram[off - 0x0C00u] = (uint8_t)(v >> 8);
            m->cartram[off - 0x0C00u + 1u] = (uint8_t)v;
        } else if (off >= 0x0FFEu) {
            m->cart_bank = (uint8_t)(((v >> 8) & 3u) % nbanks8k(m));
        }
        break;
    case CART_MINIMEM:
        if (off >= 0x1000u) {
            m->cartram[off - 0x1000u] = (uint8_t)(v >> 8);
            m->cartram[off - 0x1000u + 1u] = (uint8_t)v;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ GROM */

static uint8_t grom_data_read(ti99 *m)
{
    uint8_t r = m->grom_buf;
    uint16_t a = m->grom_addr;
    m->grom_buf = m->grom[a];
    m->grom_addr = (uint16_t)((a & 0xE000u) | ((a + 1u) & 0x1FFFu));
    m->grom_wt = m->grom_rt = 0;
    return r;
}

static uint8_t grom_addr_read(ti99 *m)
{
    uint8_t r;
    m->grom_wt = 0;
    if (!m->grom_rt) { m->grom_rt = 1; r = (uint8_t)(m->grom_addr >> 8); }
    else             { m->grom_rt = 0; r = (uint8_t)m->grom_addr; }
    return r;
}

static void grom_addr_write(ti99 *m, uint8_t d)
{
    m->grom_rt = 0;
    if (!m->grom_wt) { m->grom_hi = d; m->grom_wt = 1; }
    else {
        uint16_t a = (uint16_t)((m->grom_hi << 8) | d);
        m->grom_buf = m->grom[a];
        m->grom_addr = (uint16_t)((a & 0xE000u) | ((a + 1u) & 0x1FFFu));
        m->grom_wt = 0;
    }
}

/* ------------------------------------------------------------------- bus */

static unsigned exp_index(uint16_t a)
{
    return (a < 0x4000u) ? (unsigned)(a - 0x2000u) : (unsigned)(a - 0xA000u) + 0x2000u;
}

static uint16_t bus_read(void *ctx, uint16_t a)
{
    ti99 *m = (ti99 *)ctx;
    if (a < 0x2000u)
        return (uint16_t)((m->rom[a] << 8) | m->rom[a + 1u]);
    if (a >= 0x8000u && a < 0x8400u) {
        unsigned i = a & 0xFFu;
        return (uint16_t)((m->pad[i] << 8) | m->pad[i + 1u]);
    }
    m->cpu.extra += 4;
    if (a < 0x4000u) {
        if (!m->expansion) return 0;
        { unsigned i = exp_index(a); return (uint16_t)((m->exp[i] << 8) | m->exp[i + 1u]); }
    }
    if (a < 0x6000u) return 0;                         /* peripheral DSR space: nothing plugged in */
    if (a < 0x8000u) return cart_read(m, a);
    if (a >= 0xA000u) {
        if (!m->expansion) return 0;
        { unsigned i = exp_index(a); return (uint16_t)((m->exp[i] << 8) | m->exp[i + 1u]); }
    }
    if (a >= 0x8800u && a < 0x8C00u) {
        uint8_t d = (a & 2u) ? vdp_read_status(&m->vdp) : vdp_read_data(&m->vdp);
        return (uint16_t)(d << 8);
    }
    if (a >= 0x9000u && a < 0x9400u) {
        if (m->speech_on) {                            /* speech synthesizer status / data */
            uint8_t d;
            m->cpu.extra += (uint32_t)speech_read(&m->sp, &d);
            return (uint16_t)(d << 8);
        }
        return 0;
    }
    if (a >= 0x9800u && a < 0x9C00u) {
        uint8_t d = (a & 2u) ? grom_addr_read(m) : grom_data_read(m);
        return (uint16_t)(d << 8);
    }
    return 0;                                          /* sound, write-only ports */
}

static void bus_write(void *ctx, uint16_t a, uint16_t v)
{
    ti99 *m = (ti99 *)ctx;
    if (a < 0x2000u) return;                           /* console ROM */
    if (a >= 0x8000u && a < 0x8400u) {
        unsigned i = a & 0xFFu;
        m->pad[i] = (uint8_t)(v >> 8);
        m->pad[i + 1u] = (uint8_t)v;
        return;
    }
    m->cpu.extra += 4;
    if (a < 0x4000u) {
        if (m->expansion) { unsigned i = exp_index(a); m->exp[i] = (uint8_t)(v >> 8); m->exp[i + 1u] = (uint8_t)v; }
        return;
    }
    if (a < 0x6000u) return;
    if (a < 0x8000u) { cart_write(m, a, v); return; }
    if (a >= 0xA000u) {
        if (m->expansion) { unsigned i = exp_index(a); m->exp[i] = (uint8_t)(v >> 8); m->exp[i + 1u] = (uint8_t)v; }
        return;
    }
    if (a >= 0x8400u && a < 0x8800u) { psg_write(&m->psg, (uint8_t)(v >> 8)); m->cpu.extra += 28; return; }
    if (a >= 0x8C00u && a < 0x9000u) {
        if (a & 2u) vdp_write_ctrl(&m->vdp, (uint8_t)(v >> 8));
        else        vdp_write_data(&m->vdp, (uint8_t)(v >> 8));
        return;
    }
    if (a >= 0x9400u && a < 0x9800u) {
        if (m->speech_on) m->cpu.extra += (uint32_t)speech_write(&m->sp, (uint8_t)(v >> 8));
        return;
    }
    if (a >= 0x9C00u && a < 0xA000u) {
        if (a & 2u) grom_addr_write(m, (uint8_t)(v >> 8));
        return;                                        /* GROM data writes ignored (no GRAM) */
    }
}

static void bus_touch(void *ctx, uint16_t a)
{
    ti99 *m = (ti99 *)ctx;
    if (a < 0x2000u || (a >= 0x8000u && a < 0x8400u)) return;
    m->cpu.extra += 4;
}

/* ---------------------------------------------------------------- TMS9901 */

static int vdp_irq(const ti99 *m) { return vdp_int_line(&m->vdp); }

static int pic_irq(const ti99 *m)
{
    return (((m->pic_mask & 4u) && vdp_irq(m)) || ((m->pic_mask & 8u) && m->timer_int)) ? 1 : 0;
}

static unsigned keyboard_column(const ti99 *m)
{
    return (unsigned)(m->pout[2] | (m->pout[3] << 1) | (m->pout[4] << 2));
}

static int cru_in(void *ctx, uint16_t bit)
{
    ti99 *m = (ti99 *)ctx;
    unsigned b;
    if (bit >= 0x0800u) return 1;                      /* no peripherals: floating high */
    b = bit & 0x1Fu;
    if (b == 0) return m->timer_mode;
    if (b < 16u) {
        if (m->timer_mode) {
            if (b <= 14u) return (m->timer_latch >> (b - 1u)) & 1;
            return 0;
        }
        if (b == 1u) return 1;                         /* external interrupt: inactive */
        if (b == 2u) return !vdp_irq(m);               /* VDP interrupt, active low */
        if (b >= 3u && b <= 10u) {
            unsigned row = b - 3u;
            if (row == 4u && m->alphalock && !m->pout[5]) return 0;
            return !((m->keys[keyboard_column(m)] >> row) & 1u);
        }
        return 1;
    }
    if (b < 23u) return m->pout[b - 16u];
    return 1;
}

static void cru_out(void *ctx, uint16_t bit, int val)
{
    ti99 *m = (ti99 *)ctx;
    unsigned b;
    if (bit >= 0x0800u) return;
    b = bit & 0x1Fu;
    val = val ? 1 : 0;

    if (b == 0) {
        if (val && !m->timer_mode) m->timer_latch = m->timer_cnt;
        if (!val && m->timer_mode) { m->timer_cnt = m->timer_load; m->timer_run = (m->timer_load != 0); }
        m->timer_mode = (uint8_t)val;
    } else if (b < 16u) {
        if (m->timer_mode) {
            if (b <= 14u) {
                if (val) m->timer_load = (uint16_t)(m->timer_load | (1u << (b - 1u)));
                else     m->timer_load = (uint16_t)(m->timer_load & ~(1u << (b - 1u)));
            } else if (!val) {
                m->pic_mask = 0; m->timer_mode = 0; m->timer_run = 0;
                memset(m->pout, 0, sizeof m->pout);
            }
        } else {
            if (val) m->pic_mask = (uint16_t)(m->pic_mask | (1u << b));
            else     m->pic_mask = (uint16_t)(m->pic_mask & ~(1u << b));
            if (b == 3u) m->timer_int = 0;
        }
    } else {
        m->pout[b - 16u] = (uint8_t)val;
    }
}

static void timer_advance(ti99 *m, int cycles)
{
    if (!m->timer_run) return;
    m->timer_acc += (uint32_t)cycles;
    while (m->timer_acc >= 64u) {
        m->timer_acc -= 64u;
        if (m->timer_cnt > 0) m->timer_cnt--;
        if (m->timer_cnt == 0) {
            m->timer_cnt = m->timer_load;
            m->timer_int = 1;
        }
    }
}

/* ----------------------------------------------------------------- public */

ti99 *ti99_create(unsigned sample_rate)
{
    ti99 *m = (ti99 *)calloc(1, sizeof *m);
    double per_line;
    if (!m) return NULL;
    m->sample_rate = sample_rate;
    m->cpu.ctx = m;
    m->cpu.read = bus_read;
    m->cpu.write = bus_write;
    m->cpu.touch = bus_touch;
    m->cpu.cru_in = cru_in;
    m->cpu.cru_out = cru_out;
    m->expansion = 1;
    m->sprite_limit = 4;
    m->speech_enabled = 1;
    psg_init(&m->psg, sample_rate);
    per_line = (double)sample_rate / (TI99_FPS * (double)TI99_FRAME_LINES);
    m->aud_step = (uint32_t)(per_line * 65536.0 + 0.5);
    return m;
}

void ti99_destroy(ti99 *m)
{
    if (!m) return;
    free(m->cart_rom);
    free(m);
}

static void fill_grom_gap(uint8_t *g)
{
    /* 6K GROM chips: the 2K "hole" above 6K reads as the AND of the two blocks below it. */
    unsigned i;
    for (i = 0; i < 0x800u; i++) g[0x1800u + i] = (uint8_t)(g[0x0800u + i] & g[0x1000u + i]);
}

void ti99_set_console(ti99 *m, const uint8_t *rom, const uint8_t *grom)
{
    unsigned i;
    memcpy(m->rom, rom, 0x2000);
    memcpy(m->grom, grom, 0x6000);
    for (i = 0; i < 3; i++) fill_grom_gap(m->grom + i * 0x2000u);
}

void ti99_remove_cart(ti99 *m)
{
    free(m->cart_rom);
    m->cart_rom = NULL;
    m->cart_rom_size = 0;
    m->cart_type = CART_NONE;
    m->cart_ram_size = 0;
    memset(m->grom + 0x6000, 0, 0xA000);
    memset(m->cartram, 0, sizeof m->cartram);
    m->cart_bank = 0;
}

int ti99_insert_cart(ti99 *m, const ti99_cart_desc *d)
{
    int i;
    ti99_remove_cart(m);
    m->cart_type = d->type;
    if (d->rom_size) {
        m->cart_rom = (uint8_t *)malloc(d->rom_size + 2u);
        if (!m->cart_rom) { m->cart_type = CART_NONE; return -1; }
        memcpy(m->cart_rom, d->rom, d->rom_size);
        m->cart_rom_size = d->rom_size;
    }
    for (i = 0; i < d->ngrom; i++) {
        const ti99_blob *b = &d->grom[i];
        if (b->addr < 0x6000u || b->addr >= 0x10000u) continue;
        if (b->size > 0x10000u - b->addr) continue;
        memcpy(m->grom + b->addr, b->data, b->size);
        if (b->size == 0x1800u && (b->addr & 0x1FFFu) == 0) fill_grom_gap(m->grom + b->addr);
    }
    m->cart_ram_size = d->ram_size > sizeof m->cartram ? (uint32_t)sizeof m->cartram : d->ram_size;
    return 0;
}

void ti99_set_options(ti99 *m, int expansion_32k, int sprite_limit)
{
    m->expansion = expansion_32k ? 1 : 0;
    m->sprite_limit = (uint8_t)(sprite_limit >= 32 ? 32 : 4);
}

int ti99_set_speech_rom(ti99 *m, const uint8_t *rom)
{
    if (!rom) {
        m->speech_has_rom = 0;
        m->speech_on = 0;
        return 0;
    }
    memcpy(m->speech_rom, rom, SPEECH_ROM_SIZE);
    m->speech_has_rom = 1;
    speech_init(&m->sp, m->speech_rom);
    m->speech_on = m->speech_enabled ? 1 : 0;
    return 0;
}

void ti99_set_speech_enabled(ti99 *m, int enabled)
{
    uint8_t on;
    m->speech_enabled = enabled ? 1 : 0;
    on = (uint8_t)(m->speech_enabled && m->speech_has_rom);
    if (on && !m->speech_on) { m->sp.rom = m->speech_rom; speech_reset(&m->sp); }
    m->speech_on = on;
}

int ti99_speech_active(const ti99 *m) { return m->speech_on; }
int ti99_speech_talking(const ti99 *m) { return m->speech_on && speech_talking(&m->sp); }

void ti99_set_sprite_limit(ti99 *m, int sprite_limit)
{
    m->sprite_limit = (uint8_t)(sprite_limit >= 32 ? 32 : 4);
}

void ti99_reset(ti99 *m)
{
    memset(m->pad, 0, sizeof m->pad);
    memset(m->exp, 0, sizeof m->exp);
    vdp_reset(&m->vdp);
    memset(m->vdp.vram, 0, sizeof m->vdp.vram);
    psg_reset(&m->psg);
    m->grom_addr = 0; m->grom_buf = m->grom[0]; m->grom_wt = m->grom_rt = 0; m->grom_hi = 0;
    m->cart_bank = 0;
    m->pic_mask = 0; m->timer_mode = 0; m->timer_int = 0; m->timer_run = 0;
    m->timer_load = m->timer_cnt = m->timer_latch = 0; m->timer_acc = 0;
    memset(m->pout, 0, sizeof m->pout);
    m->budget = 0; m->aud_acc = 0;
    if (m->speech_has_rom) { m->sp.rom = m->speech_rom; speech_reset(&m->sp); }
    memset(m->fb, 0, sizeof m->fb);
    tms9900_reset(&m->cpu);
}

void ti99_set_keys(ti99 *m, const uint8_t keys[8], int alphalock)
{
    memcpy(m->keys, keys, 8);
    m->alphalock = alphalock ? 1 : 0;
}

void ti99_run_cycles(ti99 *m, int cycles)
{
    m->budget += cycles;
    while (m->budget > 0) {
        int c;
        m->cpu.intreq = (uint8_t)pic_irq(m);
        c = tms9900_step(&m->cpu);
        m->budget -= c;
        timer_advance(m, c);
        if (m->speech_on) speech_advance(&m->sp, c);
    }
}

void ti99_run_frame(ti99 *m, int16_t *audio, int *nframes)
{
    int line, n = 0;
    for (line = 0; line < TI99_FRAME_LINES; line++) {
        uint32_t cnt;
        ti99_run_cycles(m, TI99_CYCLES_PER_LINE);
        if (line < VDP_HEIGHT)
            vdp_render_line(&m->vdp, line, m->fb + line * VDP_WIDTH, m->sprite_limit);
        if (line == VDP_HEIGHT - 1)
            vdp_set_frame_flag(&m->vdp);

        m->aud_acc += m->aud_step;
        cnt = m->aud_acc >> 16;
        m->aud_acc &= 0xFFFFu;
        while (cnt-- && n < TI99_MAX_AUDIO_FRAMES) {
            int s = psg_sample(&m->psg);
            if (m->speech_on) {
                s += speech_output(&m->sp) / 2;
                if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            }
            audio[2 * n] = (int16_t)s;
            audio[2 * n + 1] = (int16_t)s;
            n++;
        }
    }
    *nframes = n;
}

const uint32_t *ti99_framebuffer(const ti99 *m) { return m->fb; }

uint8_t *ti99_expansion_ram(ti99 *m, size_t *size) { *size = sizeof m->exp; return m->exp; }
uint8_t *ti99_cart_ram(ti99 *m, size_t *size) { *size = m->cart_ram_size; return m->cart_ram_size ? m->cartram : NULL; }

uint16_t ti99_peek16(ti99 *m, uint16_t addr)
{
    if (addr < 0x2000u) return bus_read(m, (uint16_t)(addr & 0xFFFEu));
    if (addr >= 0x8000u && addr < 0x8400u) return bus_read(m, (uint16_t)(addr & 0xFFFEu));
    if (addr >= 0x2000u && addr < 0x4000u && m->expansion) return bus_read(m, (uint16_t)(addr & 0xFFFEu));
    if (addr >= 0xA000u && m->expansion) return bus_read(m, (uint16_t)(addr & 0xFFFEu));
    return 0;
}

void *ti99_cpu(ti99 *m) { return &m->cpu; }
vdp9918 *ti99_vdp(ti99 *m) { return &m->vdp; }

/* ------------------------------------------------------------- savestate */

typedef struct { uint8_t *p; size_t off; } wbuf;

static void put(wbuf *w, const void *src, size_t n) { memcpy(w->p + w->off, src, n); w->off += n; }
static void get(wbuf *w, void *dst, size_t n) { memcpy(dst, w->p + w->off, n); w->off += n; }


size_t ti99_state_size(void)
{
    return 8 + TMS9900_SAVED_SIZE + sizeof(vdp9918) + sizeof(psg76489)
         + 256 + 0x8000 + 0x1000 + 64 + 16 + SPEECH_SAVED_SIZE;
}

int ti99_save_state(const ti99 *m, void *buf, size_t size)
{
    wbuf w;
    uint32_t hdr[2];
    uint8_t sc[64];
    size_t o = 0;
    if (size < ti99_state_size()) return 0;
    memset(buf, 0, size);
    w.p = (uint8_t *)buf; w.off = 0;
    hdr[0] = STATE_MAGIC; hdr[1] = STATE_VERSION;
    put(&w, hdr, sizeof hdr);
    put(&w, &m->cpu, TMS9900_SAVED_SIZE);
    put(&w, &m->vdp, sizeof m->vdp);
    put(&w, &m->psg, sizeof m->psg);
    put(&w, m->pad, sizeof m->pad);
    put(&w, m->exp, sizeof m->exp);
    put(&w, m->cartram, sizeof m->cartram);
    memset(sc, 0, sizeof sc);
    sc[o++] = (uint8_t)m->grom_addr; sc[o++] = (uint8_t)(m->grom_addr >> 8);
    sc[o++] = m->grom_buf; sc[o++] = m->grom_wt; sc[o++] = m->grom_rt; sc[o++] = m->grom_hi;
    sc[o++] = m->cart_bank;
    sc[o++] = (uint8_t)m->pic_mask; sc[o++] = (uint8_t)(m->pic_mask >> 8);
    sc[o++] = m->timer_mode; sc[o++] = m->timer_int; sc[o++] = m->timer_run;
    sc[o++] = (uint8_t)m->timer_load; sc[o++] = (uint8_t)(m->timer_load >> 8);
    sc[o++] = (uint8_t)m->timer_cnt;  sc[o++] = (uint8_t)(m->timer_cnt >> 8);
    sc[o++] = (uint8_t)m->timer_latch; sc[o++] = (uint8_t)(m->timer_latch >> 8);
    sc[o++] = (uint8_t)(m->timer_acc & 0xFFu);
    sc[o++] = (uint8_t)(m->budget & 0xFF); sc[o++] = (uint8_t)((m->budget >> 8) & 0xFF);
    sc[o++] = (uint8_t)((m->budget >> 16) & 0xFF); sc[o++] = (uint8_t)((m->budget >> 24) & 0xFF);
    sc[o++] = (uint8_t)(m->aud_acc & 0xFF); sc[o++] = (uint8_t)((m->aud_acc >> 8) & 0xFF);
    put(&w, sc, sizeof sc);
    put(&w, m->pout, sizeof m->pout);
    put(&w, &m->sp, SPEECH_SAVED_SIZE);
    return 1;
}

int ti99_load_state(ti99 *m, const void *buf, size_t size)
{
    wbuf w;
    uint32_t hdr[2];
    uint8_t sc[64];
    size_t o = 0;
    if (size < ti99_state_size()) return 0;
    w.p = (uint8_t *)(uintptr_t)buf; w.off = 0;
    get(&w, hdr, sizeof hdr);
    if (hdr[0] != STATE_MAGIC || hdr[1] != STATE_VERSION) return 0;
    {
        tms9900 keep = m->cpu;
        get(&w, &m->cpu, TMS9900_SAVED_SIZE);
        m->cpu.ctx = keep.ctx; m->cpu.read = keep.read; m->cpu.write = keep.write;
        m->cpu.touch = keep.touch; m->cpu.cru_in = keep.cru_in; m->cpu.cru_out = keep.cru_out;
    }
    get(&w, &m->vdp, sizeof m->vdp);
    get(&w, &m->psg, sizeof m->psg);
    get(&w, m->pad, sizeof m->pad);
    get(&w, m->exp, sizeof m->exp);
    get(&w, m->cartram, sizeof m->cartram);
    get(&w, sc, sizeof sc);
    m->grom_addr = (uint16_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    m->grom_buf = sc[o++]; m->grom_wt = sc[o++]; m->grom_rt = sc[o++]; m->grom_hi = sc[o++];
    m->cart_bank = sc[o++];
    m->pic_mask = (uint16_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    m->timer_mode = sc[o++]; m->timer_int = sc[o++]; m->timer_run = sc[o++];
    m->timer_load = (uint16_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    m->timer_cnt = (uint16_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    m->timer_latch = (uint16_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    m->timer_acc = sc[o++];
    m->budget = (int32_t)((uint32_t)sc[o] | ((uint32_t)sc[o + 1] << 8) | ((uint32_t)sc[o + 2] << 16) | ((uint32_t)sc[o + 3] << 24)); o += 4;
    m->aud_acc = (uint32_t)(sc[o] | (sc[o + 1] << 8)); o += 2;
    get(&w, m->pout, sizeof m->pout);
    {
        speech tmp;
        get(&w, &tmp, SPEECH_SAVED_SIZE);
        if (m->speech_on) {
            tmp.rom = m->speech_rom;
            speech_sanitize(&tmp);
            m->sp = tmp;
        }
    }
    return 1;
}
