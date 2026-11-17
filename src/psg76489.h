/* SN76489-family programmable sound generator (TMS9919 / SN94624 as used in the TI-99/4A). */
#ifndef PSG76489_H
#define PSG76489_H

#include <stdint.h>

typedef struct psg76489 {
    uint16_t tone[3];        /* 10-bit dividers */
    uint16_t cnt[3];
    uint8_t  out[3];
    uint8_t  vol[4];         /* attenuation 0 (loud) .. 15 (off); [3] is noise */
    uint8_t  noisectl;
    uint8_t  noise_out;
    uint16_t noise_cnt;
    uint16_t lfsr;
    uint8_t  latch;          /* last latched register (0..7) */
    uint32_t step;           /* chip ticks per output sample, 16.16 fixed point */
    uint32_t acc;
} psg76489;

void psg_init(psg76489 *p, unsigned sample_rate);
void psg_reset(psg76489 *p);
void psg_write(psg76489 *p, uint8_t d);
int16_t psg_sample(psg76489 *p);      /* one mono output sample */

#endif
