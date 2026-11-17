/* TMS9900 CPU core.  Portable C99, no platform dependencies.
 *
 * The bus is word oriented (like the real chip): all accesses are 16 bit at an
 * even address.  Byte instructions perform a read-modify-write of the word, so
 * memory-mapped devices see exactly what they would see on a real console.
 */
#ifndef CPU9900_H
#define CPU9900_H

#include <stdint.h>
#include <stddef.h>

#define ST_LGT 0x8000u /* logical greater than   */
#define ST_AGT 0x4000u /* arithmetic greater than */
#define ST_EQ  0x2000u /* equal                  */
#define ST_C   0x1000u /* carry                  */
#define ST_OV  0x0800u /* overflow               */
#define ST_OP  0x0400u /* odd parity             */
#define ST_X   0x0200u /* XOP in progress        */

typedef struct tms9900 {
    /* ---- saved in savestates (everything before 'ctx') ---- */
    uint16_t pc, wp, st;
    uint8_t  idle;
    uint8_t  intreq;      /* level-1 interrupt request line (input) */
    uint32_t extra;       /* wait-state clocks added by the bus during one step */
    uint32_t pad_;        /* keeps the saved block the same size on 32- and 64-bit builds */
    /* ---- not saved ---- */
    void *ctx;
    uint16_t (*read)(void *ctx, uint16_t addr);            /* word read  */
    void     (*write)(void *ctx, uint16_t addr, uint16_t v); /* word write */
    void     (*touch)(void *ctx, uint16_t addr);           /* bus cycle, no side effects (wait states only) */
    int      (*cru_in)(void *ctx, uint16_t bit);
    void     (*cru_out)(void *ctx, uint16_t bit, int value);
} tms9900;

#define TMS9900_SAVED_SIZE offsetof(tms9900, ctx)

void tms9900_reset(tms9900 *c);
/* Execute one instruction (or take an interrupt, or idle).  Returns CPU clocks used
 * (3 MHz clock), including bus wait states added through c->extra. */
int  tms9900_step(tms9900 *c);

#endif
