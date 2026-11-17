/* TMS5200 / CD2501E speech processor + TMS6100 VSM, see speech.h.
 *
 * This is a C99 port of the logic in MAME's tms5220.cpp and tms6100.cpp
 * (BSD-3-Clause; copyright holders: Frank Palazzolo, Aaron Giles, Jonathan Gevaryahu,
 * Raphael Nabet, Couriersud, hap).  Register/field names follow MAME so the two can be
 * compared side by side.  See third_party/MAME-NOTICE.txt for the full licence text.
 *
 * Not modelled: the INT pin (not wired in the TI-99 sidecar), the 5220C rate-control
 * commands (the CD2501E treats them as NOPs), cycle-exact READY timing (an access costs a
 * fixed 75 CPU clocks, FIFO-full / pending-command stalls are modelled by running the chip).
 */
#include <string.h>
#include "speech.h"
#include "speech_tables.h"

#define NOCOMMAND 0xFFu
#define FIFO_SIZE 16
#define ACCESS_CLOCKS 75            /* 16 chip clocks of /READY low at 640 kHz = 25 us = 75 CPU clocks */
#define MAX_STALL_SAMPLES 80000     /* safety bound: ten seconds of chip time */

#define TALK_STATUS(s) ((s)->spen || (s)->talkd)

/* The saved block must be the same size on 32- and 64-bit builds (savestates, Android armeabi-v7a). */
typedef char speech_saved_size_check[(SPEECH_SAVED_SIZE == 192u) ? 1 : -1];

/* arithmetic shift right that is well defined for negative values */
static int asr(int v, int n) { return v >= 0 ? (v >> n) : ~((~v) >> n); }

/* ============================================================== TMS6100 VSM */

enum { VSM_NOP = 0, VSM_TB = 1, VSM_LA = 2, VSM_RB = 3 };

static void vsm_cmd(speech *s, unsigned cmd, unsigned add)
{
    const uint8_t *rom = s->rom;
    const uint32_t mask = SPEECH_ROM_SIZE - 1u;
    if (!rom) return;
    switch (cmd) {
    case VSM_TB:                                    /* transfer bit */
        if (s->vsm_prev_cmd == VSM_LA) {
            s->vsm_count = 0;                       /* dummy read after LA */
        } else {
            if (s->vsm_count == 0) s->vsm_sa = rom[s->vsm_addr & mask];
            else s->vsm_sa = (uint8_t)((s->vsm_sa >> 1) | ((s->vsm_sa << 7) & 0x80u));
            s->vsm_data = (uint8_t)((s->vsm_sa << 3) & 8u);      /* SA0 -> DATA/ADD8 */
            s->vsm_count = (uint8_t)((s->vsm_count + 1u) & 7u);
            if (s->vsm_count == 0) s->vsm_addr++;
        }
        break;
    case VSM_LA:                                    /* load address (one nibble per command) */
        if (s->vsm_prev_cmd == VSM_TB) {
            s->vsm_addr = (s->vsm_addr & ~0xFu) | (add & 0xFu);
            s->vsm_count = 0;
        } else if (s->vsm_prev_cmd == VSM_LA) {
            if (s->vsm_count < 4) {
                unsigned shift = 4u * (s->vsm_count + 1u);
                s->vsm_addr = (s->vsm_addr & ~(0xFu << shift)) | ((add & 0xFu) << shift);
            }
            s->vsm_count = (uint8_t)((s->vsm_count + 1u) & 7u);
        }
        break;
    case VSM_RB:                                    /* read and branch */
        if (s->vsm_prev_cmd == VSM_LA || (s->vsm_prev_cmd == VSM_TB && s->vsm_count == 0)) {
            uint32_t rb;
            s->vsm_count = 0;
            rb = rom[s->vsm_addr & mask];
            s->vsm_addr++;
            rb |= (uint32_t)rom[s->vsm_addr & mask] << 8;
            s->vsm_addr = (s->vsm_addr & ~0x3FFFu) | (rb & 0x3FFFu);
        }
        break;
    default:
        break;
    }
    s->vsm_prev_cmd = (uint8_t)cmd;
}

static unsigned vsm_read_bit(speech *s)
{
    vsm_cmd(s, VSM_TB, 0);
    return (s->vsm_data & 8u) ? 1u : 0u;
}

/* ========================================================== TMS5200 internals */

static void process_command(speech *s, uint8_t cmd);

static void perform_dummy_read(speech *s)
{
    s->sched_dummy = 0;
    if (s->rom) {
        vsm_cmd(s, VSM_LA, 0);
        vsm_cmd(s, VSM_TB, 0);
    }
}

/* Recompute BL / BE and the TALK latches after the FIFO or talk state changed. */
static void update_fifo_status(speech *s)
{
    s->buffer_low = (s->fifo_count <= 8) ? 1 : 0;
    if (s->fifo_count == 0) {
        s->buffer_empty = 1;
        if (s->ddis) s->talk = s->spen = 0;         /* BE clears TALK, only in speak-external mode */
    } else {
        s->buffer_empty = 0;
    }
    if (s->prev_talk && !TALK_STATUS(s)) {          /* talk status 1 -> 0 */
        s->ddis = 0;
        s->prev_talk = 0;
        if (s->cmd_reg != NOCOMMAND) process_command(s, s->cmd_reg);   /* resume a held command */
    }
    s->prev_talk = TALK_STATUS(s) ? 1 : 0;
}

static unsigned read_bits(speech *s, int count)
{
    unsigned val = 0;
    if (s->ddis) {                                  /* from the FIFO */
        while (count--) {
            val = (val << 1) | ((s->fifo[s->fifo_head] >> s->fifo_bits_taken) & 1u);
            s->fifo_bits_taken++;
            if (s->fifo_bits_taken >= 8) {
                s->fifo_count--;
                s->fifo[s->fifo_head] = 0;
                s->fifo_head = (uint8_t)((s->fifo_head + 1u) % FIFO_SIZE);
                s->fifo_bits_taken = 0;
                update_fifo_status(s);
            }
        }
    } else if (s->rom) {                            /* from the VSM, MSB first */
        while (count--) val = (val << 1) | vsm_read_bit(s);
    } else {
        val = (1u << count) - 1u;                   /* floating bus reads as ones -> a stop frame */
    }
    return val;
}

static void init_new_frame(speech *s)
{
    int i;
    s->new_e = 0;
    s->new_p = 0;
    for (i = 0; i < 4; i++) s->new_k[i] = 0;
    for (i = 4; i < 7; i++) s->new_k[i] = 0xF;
    for (i = 7; i < 10; i++) s->new_k[i] = 0x7;
}

static void parse_frame(speech *s)
{
    int i, rep;
    s->uv_zpar = s->zpar = 0;                       /* parsing a frame means we are talking */
    s->ip = 0;                                      /* reload_table[0] on the 5200 */

    update_fifo_status(s);
    if (s->ddis && s->buffer_empty) return;

    s->new_e = (uint8_t)read_bits(s, 4);
    update_fifo_status(s);
    if (s->ddis && s->buffer_empty) return;
    if (s->new_e == 0 || s->new_e == 15) return;    /* silence / stop frame */

    rep = (int)read_bits(s, 1);
    s->new_p = (uint8_t)read_bits(s, 6);
    s->uv_zpar = (s->new_p == 0) ? 1 : 0;           /* unvoiced: K5..K10 are zero */
    update_fifo_status(s);
    if (s->ddis && s->buffer_empty) return;
    if (rep) return;                                /* repeat frame reuses the old K's */

    for (i = 0; i < 4; i++) {
        s->new_k[i] = (uint8_t)read_bits(s, sp_k_bits[i]);
        update_fifo_status(s);
        if (s->ddis && s->buffer_empty) return;
    }
    if (s->new_p == 0) return;                      /* unvoiced frames only carry K1..K4 */
    for (i = 4; i < 10; i++) {
        s->new_k[i] = (uint8_t)read_bits(s, sp_k_bits[i]);
        update_fifo_status(s);
        if (s->ddis && s->buffer_empty) return;
    }
}

static void chip_reset(speech *s)
{
    int i;
    memset(s->fifo, 0, sizeof s->fifo);
    s->fifo_head = s->fifo_tail = s->fifo_count = s->fifo_bits_taken = 0;
    s->spen = s->ddis = s->talk = s->talkd = s->prev_talk = 0;
    s->buffer_empty = s->buffer_low = 1;
    s->cmd_reg = NOCOMMAND;
    s->rdb_flag = 0;
    s->new_e = 0; s->cur_energy = 0; s->prev_energy = 0;
    s->new_p = 0; s->cur_pitch = 0;
    s->zpar = s->uv_zpar = 0;
    for (i = 0; i < 10; i++) { s->new_k[i] = 0; s->cur_k[i] = 0; }
    s->inhibit = 1;
    s->subcycle = 0;                                /* as MAME: the first sample counts it up to 1 */
    s->pitch_count = 0;
    s->pc = 0;
    s->olde = s->oldp = 1;
    s->ip = 0;
    s->rng = 0x1FFF;
    memset(s->u, 0, sizeof s->u);
    memset(s->x, 0, sizeof s->x);
    perform_dummy_read(s);
}

static void process_command(speech *s, uint8_t cmd)
{
    s->cmd_reg = cmd;
    switch (cmd & 0x70u) {
    case 0x10:                                      /* read byte (from VSM) */
        if (!TALK_STATUS(s)) {
            if (s->sched_dummy) perform_dummy_read(s);
            s->read_byte_reg = (uint8_t)read_bits(s, 8);
            s->rdb_flag = 1;
            s->cmd_reg = NOCOMMAND;
        }
        break;
    case 0x00:
    case 0x20:                                      /* NOP on the CD2501E */
        s->cmd_reg = NOCOMMAND;
        break;
    case 0x30:                                      /* read and branch */
        if (!TALK_STATUS(s)) {
            s->rdb_flag = 0;
            if (s->sched_dummy) perform_dummy_read(s);
            vsm_cmd(s, VSM_RB, 0);
            s->cmd_reg = NOCOMMAND;
        }
        break;
    case 0x40:                                      /* load address nibble */
        if (!TALK_STATUS(s)) {
            vsm_cmd(s, VSM_LA, cmd & 0x0Fu);
            s->sched_dummy = 1;
            s->cmd_reg = NOCOMMAND;
        }                                           /* else: held until speech ends */
        break;
    case 0x50:                                      /* speak (from VSM) */
        if (s->sched_dummy) perform_dummy_read(s);
        s->spen = 1;
        s->talk = 1;                                /* MAME FAST_START_HACK */
        s->ddis = 0;
        s->zpar = 1; s->uv_zpar = 1; s->olde = 1; s->oldp = 1;
        init_new_frame(s);
        s->cmd_reg = NOCOMMAND;
        break;
    case 0x60:                                      /* speak external (from the FIFO) */
        memset(s->fifo, 0, sizeof s->fifo);
        s->fifo_head = s->fifo_tail = s->fifo_count = s->fifo_bits_taken = 0;
        s->ddis = 1;
        s->zpar = 1; s->uv_zpar = 1; s->olde = 1; s->oldp = 1;
        init_new_frame(s);
        s->rdb_flag = 0;
        s->cmd_reg = NOCOMMAND;
        break;
    case 0x70:                                      /* reset */
        if (s->sched_dummy) perform_dummy_read(s);
        chip_reset(s);
        s->cmd_reg = NOCOMMAND;
        break;
    }
    update_fifo_status(s);
}

static void data_write(speech *s, uint8_t data)
{
    if (s->ddis) {
        if (s->fifo_count < FIFO_SIZE) {
            uint8_t old_low = s->buffer_low;
            s->fifo[s->fifo_tail] = data;
            s->fifo_tail = (uint8_t)((s->fifo_tail + 1u) % FIFO_SIZE);
            s->fifo_count++;
            update_fifo_status(s);
            if (!s->spen && old_low && !s->buffer_low) {      /* FIFO passed half full: start talking */
                s->zpar = 1; s->uv_zpar = 1; s->olde = 1; s->oldp = 1;
                s->spen = 1;
                s->talk = 1;                                  /* FAST_START_HACK */
                init_new_frame(s);
            }
        }
    } else {
        process_command(s, data);
    }
}

/* ------------------------------------------------------------ sample generator */

static int matrix_multiply(int a, int b)
{
    while (a > 511) a -= 1024;                      /* K is 10 bits */
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;                   /* running result is 14 bits */
    while (b < -16384) b += 32768;
    return asr(a * b, 9);
}

static int lattice_filter(speech *s)
{
    int i;
    int k[10];
    for (i = 0; i < 10; i++) k[i] = s->cur_k[i];
    s->u[10] = matrix_multiply((int)s->prev_energy, s->excitation * 64);
    for (i = 9; i >= 0; i--) s->u[i] = s->u[i + 1] - matrix_multiply(k[i], s->x[i]);
    for (i = 9; i >= 1; i--) s->x[i] = s->x[i - 1] + matrix_multiply(k[i - 1], s->u[i - 1]);
    s->x[0] = s->u[0];
    s->prev_energy = (uint16_t)s->cur_energy;
    return s->u[0];
}

/* 14-bit lattice output -> 10-bit analog pin, range-extended to 16 bits (MAME clip_analog) */
static int16_t clip_analog(int v)
{
    if (v > 2047) v = 2047;
    else if (v < -2048) v = -2048;
    v &= ~0xF;
    return (int16_t)(v * 16 | ((v & 0x7F0) >> 3) | ((v & 0x400) >> 10));
}

static int16_t make_sample(speech *s)
{
    int16_t out;
    if (s->talkd) {
        if (s->ip == 0 && s->pc == 12 && s->subcycle == 1) {
            /* a new frame is applied */
            s->ip = 0;
            parse_frame(s);
            if (s->new_e == 0x0F) {                 /* stop frame */
                s->talk = s->spen = 0;
                update_fifo_status(s);
            }
            {
                int new_unv = (s->new_p == 0), new_sil = (s->new_e == 0);
                if ((!s->oldp && new_unv) || (s->oldp && !new_unv) ||
                    (s->olde && !new_sil) || (s->oldp && new_sil))
                    s->inhibit = 1;
                else
                    s->inhibit = 0;
            }
        } else if (s->subcycle == 2) {
            /* parameters are interpolated on the B sub-cycle of each parameter slot */
            int inh = (s->inhibit && s->ip != 0) ? 1 : 0;
            int shift = sp_interp_shift[s->ip];
            if (s->pc == 0) {
                if (s->ip == 0) s->pitch_zero = 0;
                s->cur_energy = (int16_t)((s->cur_energy +
                    asr(((int)sp_energy_tab[s->new_e] - s->cur_energy) * (1 - inh), shift)) * (1 - s->zpar));
            } else if (s->pc == 1) {
                s->cur_pitch = (int16_t)((s->cur_pitch +
                    asr(((int)sp_pitch_tab[s->new_p] - s->cur_pitch) * (1 - inh), shift)) * (1 - s->zpar));
            } else if (s->pc >= 2 && s->pc <= 11) {
                int idx = s->pc - 2;
                int z = (idx < 4) ? s->zpar : s->uv_zpar;
                s->cur_k[idx] = (int16_t)((s->cur_k[idx] +
                    asr(((int)sp_k_tab[idx][s->new_k[idx]] - s->cur_k[idx]) * (1 - inh), shift)) * (1 - z));
            }
        }

        if (s->oldp) {                              /* unvoiced: +/- half of the chirp maximum */
            s->excitation = (s->rng & 1u) ? -64 : 0x40;
        } else {                                    /* voiced: chirp ROM, held at the last entry */
            s->excitation = sp_chirp_tab[s->pitch_count >= 51 ? 51 : s->pitch_count];
        }

        {
            int i, v;
            for (i = 0; i < 20; i++) {              /* the LFSR runs 20 times per sample */
                unsigned bit = ((s->rng >> 12) ^ (s->rng >> 3) ^ (s->rng >> 2) ^ s->rng) & 1u;
                s->rng = (uint16_t)((s->rng << 1) | bit);
            }
            v = lattice_filter(s);
            while (v > 16383) v -= 32768;
            while (v < -16384) v += 32768;
            out = clip_analog(v);
        }

        s->subcycle++;
        if (s->subcycle == 2 && s->pc == 12) {
            if (s->ip == 7 && s->inhibit) s->pitch_zero = 1;
            if (s->ip == 7) {                       /* RESETL4: latch the new frame's flags */
                s->olde = (s->new_e == 0);
                s->oldp = (s->new_p == 0);
                s->talkd = s->talk;
                update_fifo_status(s);
                if (!s->talk && s->spen) s->talk = 1;
            }
            s->subcycle = 1;
            s->pc = 0;
            s->ip = (uint8_t)((s->ip + 1u) & 7u);
        } else if (s->subcycle == 3) {
            s->subcycle = 1;
            s->pc++;
        }
        s->pitch_count++;
        if (s->pitch_count >= (unsigned)(s->cur_pitch < 0 ? 0 : s->cur_pitch) || s->pitch_zero) s->pitch_count = 0;
        s->pitch_count &= 0x1FFu;
    } else {                                        /* not talking: keep the frame clock running */
        s->subcycle++;
        if (s->subcycle == 2 && s->pc == 12) {
            if (s->ip == 7) {
                s->talkd = s->talk;
                update_fifo_status(s);
                if (!s->talk && s->spen) s->talk = 1;
            }
            s->subcycle = 1;
            s->pc = 0;
            s->ip = (uint8_t)((s->ip + 1u) & 7u);
        } else if (s->subcycle == 3) {
            s->subcycle = 1;
            s->pc++;
        }
        out = -1;                                   /* the chip idles at -1 */
    }
    return out;
}

static void step_sample(speech *s)
{
    s->out_prev = s->out_cur;
    s->out_cur = make_sample(s);
}

/* ================================================================== public API */

void speech_init(speech *s, const uint8_t *rom)
{
    memset(s, 0, sizeof *s);
    s->rom = rom;
    s->cmd_reg = NOCOMMAND;
    s->out_prev = s->out_cur = -1;
    chip_reset(s);
}

void speech_reset(speech *s)
{
    chip_reset(s);
    s->cyc_acc = 0;
    s->credit = 0;
    s->out_prev = s->out_cur = -1;
}

int speech_talking(const speech *s) { return TALK_STATUS(s) ? 1 : 0; }

int speech_read(speech *s, uint8_t *value)
{
    if (s->rdb_flag) {                              /* last command was read-byte: return the data */
        s->rdb_flag = 0;
        *value = s->read_byte_reg;
    } else {
        *value = (uint8_t)((TALK_STATUS(s) ? 0x80u : 0u) | (s->buffer_low ? 0x40u : 0u) |
                           (s->buffer_empty ? 0x20u : 0u));
    }
    return ACCESS_CLOCKS;
}

int speech_write(speech *s, uint8_t value)
{
    int n = 0;
    /* /READY stays low while a held command is pending or the FIFO is full: run the chip. */
    while (s->cmd_reg != NOCOMMAND && n < MAX_STALL_SAMPLES) { step_sample(s); n++; }
    if (s->ddis)
        while (s->fifo_count >= FIFO_SIZE && n < MAX_STALL_SAMPLES) { step_sample(s); n++; }
    s->credit += n * SPEECH_CLOCKS_PER_SAMPLE;
    data_write(s, value);
    return ACCESS_CLOCKS + n * SPEECH_CLOCKS_PER_SAMPLE;
}

void speech_advance(speech *s, int cycles)
{
    if (s->credit > 0) {                            /* already run ahead during a bus stall */
        if (s->credit >= cycles) { s->credit -= cycles; return; }
        cycles -= s->credit;
        s->credit = 0;
    }
    s->cyc_acc += cycles;
    while (s->cyc_acc >= SPEECH_CLOCKS_PER_SAMPLE) {
        s->cyc_acc -= SPEECH_CLOCKS_PER_SAMPLE;
        step_sample(s);
    }
}

int speech_output(const speech *s)
{
    int d = (int)s->out_cur - (int)s->out_prev;
    return (int)s->out_prev + (d * s->cyc_acc) / SPEECH_CLOCKS_PER_SAMPLE;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void speech_sanitize(speech *s)
{
    int i;
    s->fifo_head &= 15u; s->fifo_tail &= 15u;
    if (s->fifo_count > FIFO_SIZE) s->fifo_count = FIFO_SIZE;
    s->fifo_bits_taken &= 7u;
    s->new_e &= 15u; s->new_p &= 63u;
    for (i = 0; i < 10; i++) {
        s->new_k[i] &= (uint8_t)((1u << sp_k_bits[i]) - 1u);
        s->cur_k[i] = (int16_t)clampi(s->cur_k[i], -1024, 1023);
    }
    if (s->subcycle > 2) s->subcycle = 0;
    if (s->pc > 12) s->pc = 0;
    s->ip &= 7u;
    s->vsm_count &= 7u;
    if (s->vsm_prev_cmd > 3) s->vsm_prev_cmd = 0;
    s->vsm_data &= 8u;
    s->pitch_count &= 0x1FFu;
    s->cur_energy = (int16_t)clampi(s->cur_energy, 0, 511);
    s->cur_pitch = (int16_t)clampi(s->cur_pitch, 0, 511);
    s->excitation = (int16_t)clampi(s->excitation, -128, 127);
    for (i = 0; i < 11; i++) s->u[i] = clampi(s->u[i], -65535, 65535);
    for (i = 0; i < 10; i++) s->x[i] = clampi(s->x[i], -65535, 65535);
    s->cyc_acc = clampi(s->cyc_acc, 0, SPEECH_CLOCKS_PER_SAMPLE - 1);
    s->credit = clampi(s->credit, 0, MAX_STALL_SAMPLES * SPEECH_CLOCKS_PER_SAMPLE);
    s->vsm_addr &= 0xFFFFFu;
    {   /* flags are booleans */
        uint8_t *f[] = { &s->spen, &s->ddis, &s->talk, &s->talkd, &s->buffer_low, &s->buffer_empty, &s->prev_talk,
                         &s->olde, &s->oldp, &s->inhibit, &s->uv_zpar, &s->zpar, &s->pitch_zero, &s->rdb_flag, &s->sched_dummy };
        for (i = 0; i < (int)(sizeof f / sizeof f[0]); i++) *f[i] = *f[i] ? 1 : 0;
    }
}
