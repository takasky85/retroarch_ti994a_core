/* SN76489-family sound generator -- see psg76489.h */
#include <string.h>
#include "psg76489.h"

#define PSG_CLOCK 3579545u

/* 2 dB steps */
static const int16_t vol_table[16] = {
    3000, 2383, 1893, 1504, 1195, 949, 754, 599, 476, 378, 300, 238, 189, 150, 119, 0
};

void psg_reset(psg76489 *p)
{
    uint32_t step = p->step;
    memset(p, 0, sizeof *p);
    p->step = step;
    p->vol[0] = p->vol[1] = p->vol[2] = p->vol[3] = 15;
    p->lfsr = 0x4000;
    p->tone[0] = p->tone[1] = p->tone[2] = 0x400;
    p->cnt[0] = p->cnt[1] = p->cnt[2] = 1;
}

void psg_init(psg76489 *p, unsigned sample_rate)
{
    /* chip ticks at clock/16 */
    double ticks = ((double)PSG_CLOCK / 16.0) / (double)sample_rate;
    p->step = (uint32_t)(ticks * 65536.0 + 0.5);
    psg_reset(p);
}

void psg_write(psg76489 *p, uint8_t d)
{
    if (d & 0x80u) {
        unsigned reg = (d >> 4) & 7u;
        unsigned lo = d & 15u;
        p->latch = (uint8_t)reg;
        switch (reg) {
        case 0: case 2: case 4:
            p->tone[reg >> 1] = (uint16_t)((p->tone[reg >> 1] & 0x3F0u) | lo);
            break;
        case 6:
            p->noisectl = (uint8_t)(lo & 7u);
            p->lfsr = 0x4000;
            break;
        default:
            p->vol[reg >> 1] = (uint8_t)lo;
            break;
        }
    } else {
        unsigned reg = p->latch;
        switch (reg) {
        case 0: case 2: case 4:
            p->tone[reg >> 1] = (uint16_t)((p->tone[reg >> 1] & 0x00Fu) | ((unsigned)(d & 0x3Fu) << 4));
            break;
        case 6:
            p->noisectl = (uint8_t)(d & 7u);
            p->lfsr = 0x4000;
            break;
        default:
            p->vol[reg >> 1] = (uint8_t)(d & 15u);
            break;
        }
    }
}

static unsigned parity16(unsigned v)
{
    v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return v & 1u;
}

static void tick(psg76489 *p)
{
    int i;
    for (i = 0; i < 3; i++) {
        unsigned period = p->tone[i] ? p->tone[i] : 0x400u;
        if (p->cnt[i] > 0) p->cnt[i]--;
        if (p->cnt[i] == 0) {
            p->cnt[i] = (uint16_t)period;
            p->out[i] ^= 1u;
        }
    }
    {
        unsigned nf = p->noisectl & 3u;
        unsigned period = (nf == 3) ? (p->tone[2] ? p->tone[2] : 0x400u) : (0x10u << nf);
        if (p->noise_cnt > 0) p->noise_cnt--;
        if (p->noise_cnt == 0) {
            p->noise_cnt = (uint16_t)period;
            p->noise_out ^= 1u;
            if (p->noise_out) {                       /* shift on rising edge */
                unsigned fb = (p->noisectl & 4u) ? parity16(p->lfsr & 0x0003u) : (p->lfsr & 1u);
                p->lfsr = (uint16_t)((p->lfsr >> 1) | (fb << 14));
            }
        }
    }
}

int16_t psg_sample(psg76489 *p)
{
    int32_t sum = 0;
    unsigned n, i, k;

    p->acc += p->step;
    n = p->acc >> 16;
    p->acc &= 0xFFFFu;
    if (n == 0) n = 1;

    for (k = 0; k < n; k++) {
        tick(p);
        for (i = 0; i < 3; i++) {
            int lvl = vol_table[p->vol[i]];
            int hi = (p->tone[i] <= 1) ? 1 : p->out[i];   /* ultrasonic -> DC */
            sum += hi ? lvl : -lvl;
        }
        sum += (p->lfsr & 1u) ? vol_table[p->vol[3]] : -vol_table[p->vol[3]];
    }
    return (int16_t)(sum / (int32_t)n);
}
