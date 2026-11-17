/* TI-99/4A Speech Synthesizer: TMS5200 (CD2501E / TMC0285) + two TMS6100 vocabulary ROMs.
 *
 * Portable C99, integer only, no allocation.  The synthesis algorithm, interpolation,
 * lattice filter and VSM serial protocol follow MAME's tms5220.cpp / tms6100.cpp
 * (BSD-3-Clause, see third_party/MAME-NOTICE.txt).
 *
 * Timing: the chip makes one 8 kHz sample every 375 CPU clocks (3 MHz / 375 = 8000 Hz).
 * The host calls speech_advance() with the CPU clocks that elapsed.
 */
#ifndef SPEECH_H
#define SPEECH_H

#include <stdint.h>
#include <stddef.h>

#define SPEECH_ROM_SIZE      0x8000u     /* cd2325a.u2a (16K) + cd2326a.u2b (16K) */
#define SPEECH_CLOCKS_PER_SAMPLE 375     /* 3 MHz CPU clock / 8 kHz */

typedef struct speech {
    /* ---------------------------------------------- saved state (everything before 'rom') */
    uint32_t vsm_addr;
    int32_t  u[11];
    int32_t  x[10];
    int32_t  cyc_acc;            /* CPU clocks since the last sample was produced           */
    int32_t  credit;             /* clocks the chip was already run ahead (bus stalls)       */
    int16_t  cur_energy, cur_pitch;
    int16_t  cur_k[10];
    int16_t  excitation;
    int16_t  out_prev, out_cur;  /* last two output samples (for resampling)                */
    uint16_t prev_energy, rng, pitch_count;
    uint8_t  fifo[16];
    uint8_t  fifo_head, fifo_tail, fifo_count, fifo_bits_taken;
    uint8_t  spen, ddis, talk, talkd, buffer_low, buffer_empty, prev_talk;
    uint8_t  olde, oldp, inhibit, uv_zpar, zpar, pitch_zero, rdb_flag, sched_dummy;
    uint8_t  cmd_reg, read_byte_reg;
    uint8_t  new_e, new_p, new_k[10];
    uint8_t  subcycle, pc, ip;
    uint8_t  vsm_sa, vsm_count, vsm_prev_cmd, vsm_data;
    uint8_t  pad_[4];
    /* ---------------------------------------------- not saved */
    const uint8_t *rom;          /* SPEECH_ROM_SIZE bytes, or NULL (no vocabulary ROM)      */
} speech;

#define SPEECH_SAVED_SIZE offsetof(speech, rom)

/* Power-on state; 'rom' may be NULL. */
void speech_init(speech *s, const uint8_t *rom);
/* Console reset: resets the TMS5200 (a hardware RESET command), not the VSM address state. */
void speech_reset(speech *s);

/* TI bus: read of >9000 (status / read-byte data) and write of >9400 (command / speech data).
 * Both return the number of extra CPU clocks the access stalled the CPU for (READY low). */
int speech_read(speech *s, uint8_t *value);
int speech_write(speech *s, uint8_t value);

/* Advance by CPU clocks.  Cheap while idle. */
void speech_advance(speech *s, int cycles);

/* Current output, linearly resampled between the last two 8 kHz samples, -32768..32767. */
int  speech_output(const speech *s);

/* Make a speech state loaded from an untrusted savestate safe to run (indices in range, bounded filter state). */
void speech_sanitize(speech *s);

/* Introspection for tests / status display. */
int  speech_talking(const speech *s);

#endif
