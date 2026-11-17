/* TMS9900 CPU core -- see cpu9900.h */
#include "cpu9900.h"

/* ------------------------------------------------------------------ helpers */

static uint16_t rdw(tms9900 *c, uint16_t a) { return c->read(c->ctx, (uint16_t)(a & 0xFFFEu)); }
static void     wrw(tms9900 *c, uint16_t a, uint16_t v) { c->write(c->ctx, (uint16_t)(a & 0xFFFEu), v); }
static uint16_t fetch(tms9900 *c) { uint16_t v = rdw(c, c->pc); c->pc = (uint16_t)(c->pc + 2); return v; }

static uint8_t rdb(tms9900 *c, uint16_t a)
{
    uint16_t w = rdw(c, a);
    return (uint8_t)((a & 1u) ? (w & 0xFFu) : (w >> 8));
}

static void wrb(tms9900 *c, uint16_t a, uint8_t v)
{
    uint16_t w = rdw(c, a);
    if (a & 1u) w = (uint16_t)((w & 0xFF00u) | v);
    else        w = (uint16_t)((w & 0x00FFu) | ((uint16_t)v << 8));
    wrw(c, a, w);
}

static uint16_t reg_addr(const tms9900 *c, unsigned n) { return (uint16_t)(c->wp + 2u * n); }
static uint16_t getreg(tms9900 *c, unsigned n) { return rdw(c, reg_addr(c, n)); }
static void     setreg(tms9900 *c, unsigned n, uint16_t v) { wrw(c, reg_addr(c, n), v); }

static int odd8(uint8_t v) { v ^= (uint8_t)(v >> 4); v ^= (uint8_t)(v >> 2); v ^= (uint8_t)(v >> 1); return v & 1; }

static void lae16(tms9900 *c, uint16_t v)
{
    c->st &= (uint16_t)~(ST_LGT | ST_AGT | ST_EQ);
    if (v == 0) c->st |= ST_EQ;
    else { c->st |= ST_LGT; if (!(v & 0x8000u)) c->st |= ST_AGT; }
}

static void lae8(tms9900 *c, uint8_t v)
{
    c->st &= (uint16_t)~(ST_LGT | ST_AGT | ST_EQ | ST_OP);
    if (v == 0) c->st |= ST_EQ;
    else { c->st |= ST_LGT; if (!(v & 0x80u)) c->st |= ST_AGT; }
    if (odd8(v)) c->st |= ST_OP;
}

static void cmp16(tms9900 *c, uint16_t s, uint16_t d)
{
    c->st &= (uint16_t)~(ST_LGT | ST_AGT | ST_EQ);
    if (s == d) c->st |= ST_EQ;
    if (s > d) c->st |= ST_LGT;
    if ((int16_t)s > (int16_t)d) c->st |= ST_AGT;
}

static void cmp8(tms9900 *c, uint8_t s, uint8_t d)
{
    c->st &= (uint16_t)~(ST_LGT | ST_AGT | ST_EQ | ST_OP);
    if (s == d) c->st |= ST_EQ;
    if (s > d) c->st |= ST_LGT;
    if ((int8_t)s > (int8_t)d) c->st |= ST_AGT;
    if (odd8(s)) c->st |= ST_OP;
}

/* d + s with flags (word) */
static uint16_t add16(tms9900 *c, uint16_t d, uint16_t s)
{
    uint32_t r = (uint32_t)d + s;
    uint16_t res = (uint16_t)r;
    lae16(c, res);
    c->st &= (uint16_t)~(ST_C | ST_OV);
    if (r & 0x10000u) c->st |= ST_C;
    if (((s ^ res) & (d ^ res)) & 0x8000u) c->st |= ST_OV;
    return res;
}

/* d - s with flags (word) */
static uint16_t sub16(tms9900 *c, uint16_t d, uint16_t s)
{
    uint32_t r = (uint32_t)d - s;
    uint16_t res = (uint16_t)r;
    lae16(c, res);
    c->st &= (uint16_t)~(ST_C | ST_OV);
    if (d >= s) c->st |= ST_C;                       /* carry = no borrow */
    if (((d ^ s) & (d ^ res)) & 0x8000u) c->st |= ST_OV;
    return res;
}

static uint8_t add8(tms9900 *c, uint8_t d, uint8_t s)
{
    uint32_t r = (uint32_t)d + s;
    uint8_t res = (uint8_t)r;
    lae8(c, res);
    c->st &= (uint16_t)~(ST_C | ST_OV);
    if (r & 0x100u) c->st |= ST_C;
    if (((s ^ res) & (d ^ res)) & 0x80u) c->st |= ST_OV;
    return res;
}

static uint8_t sub8(tms9900 *c, uint8_t d, uint8_t s)
{
    uint32_t r = (uint32_t)d - s;
    uint8_t res = (uint8_t)r;
    lae8(c, res);
    c->st &= (uint16_t)~(ST_C | ST_OV);
    if (d >= s) c->st |= ST_C;
    if (((d ^ s) & (d ^ res)) & 0x80u) c->st |= ST_OV;
    return res;
}

/* Resolve an operand address.  'cyc' accumulates addressing-mode clocks. */
static uint16_t ea(tms9900 *c, unsigned mode, unsigned reg, int byte, int *cyc)
{
    switch (mode) {
    case 0:
        return reg_addr(c, reg);
    case 1:
        *cyc += 4;
        return getreg(c, reg);
    case 2: {
        uint16_t a = fetch(c);
        *cyc += 8;
        if (reg) a = (uint16_t)(a + getreg(c, reg));
        return a;
    }
    default: {
        uint16_t r = getreg(c, reg);
        setreg(c, reg, (uint16_t)(r + (byte ? 1 : 2)));
        *cyc += byte ? 6 : 8;
        return r;
    }
    }
}

static unsigned cru_base(tms9900 *c) { return (getreg(c, 12) >> 1) & 0x0FFFu; }

static int exec(tms9900 *c, uint16_t op);

static void context_switch(tms9900 *c, uint16_t vector, uint16_t oldpc)
{
    uint16_t oldwp = c->wp, oldst = c->st;
    uint16_t nwp = rdw(c, vector);
    uint16_t npc = rdw(c, (uint16_t)(vector + 2));
    c->wp = nwp;
    c->pc = npc;
    wrw(c, (uint16_t)(nwp + 26), oldwp);
    wrw(c, (uint16_t)(nwp + 28), oldpc);
    wrw(c, (uint16_t)(nwp + 30), oldst);
}

/* --------------------------------------------------------------- format 1 */

static int fmt1(tms9900 *c, uint16_t op)
{
    int byte = (op >> 12) & 1;
    unsigned code = op >> 13;            /* 2..7 */
    int cyc = 14;
    uint16_t sa = ea(c, (op >> 4) & 3u, op & 15u, byte, &cyc);
    unsigned sv = byte ? rdb(c, sa) : rdw(c, sa);
    uint16_t da = ea(c, (op >> 10) & 3u, (op >> 6) & 15u, byte, &cyc);

    if (code == 6) {                     /* MOV / MOVB */
        if (byte) {
            wrb(c, da, (uint8_t)sv);                    /* read-modify-write */
            lae8(c, (uint8_t)sv);
        } else {
            c->touch(c->ctx, (uint16_t)(da & 0xFFFEu));  /* dummy destination read */
            wrw(c, da, (uint16_t)sv);
            lae16(c, (uint16_t)sv);
        }
        return cyc;
    }
    if (code == 4) {                     /* C / CB */
        if (byte) cmp8(c, (uint8_t)sv, rdb(c, da));
        else      cmp16(c, (uint16_t)sv, rdw(c, da));
        return cyc;
    }

    if (byte) {
        uint8_t d = rdb(c, da), r;
        switch (code) {
        case 2:  r = (uint8_t)(d & ~sv); lae8(c, r); break;   /* SZCB */
        case 3:  r = sub8(c, d, (uint8_t)sv); break;           /* SB   */
        case 5:  r = add8(c, d, (uint8_t)sv); break;           /* AB   */
        default: r = (uint8_t)(d | sv); lae8(c, r); break;     /* SOCB */
        }
        wrb(c, da, r);
    } else {
        uint16_t d = rdw(c, da), r;
        switch (code) {
        case 2:  r = (uint16_t)(d & ~sv); lae16(c, r); break;  /* SZC */
        case 3:  r = sub16(c, d, (uint16_t)sv); break;          /* S   */
        case 5:  r = add16(c, d, (uint16_t)sv); break;          /* A   */
        default: r = (uint16_t)(d | sv); lae16(c, r); break;    /* SOC */
        }
        wrw(c, da, r);
    }
    return cyc;
}

/* ----------------------------------------------------------------- jumps */

static int fmt2(tms9900 *c, uint16_t op)
{
    int8_t disp = (int8_t)(op & 0xFF);
    unsigned st = c->st;
    int take = 0;

    switch (op >> 8) {
    case 0x10: take = 1; break;                                           /* JMP */
    case 0x11: take = !(st & ST_AGT) && !(st & ST_EQ); break;             /* JLT */
    case 0x12: take = !(st & ST_LGT) || (st & ST_EQ); break;              /* JLE */
    case 0x13: take = (st & ST_EQ) != 0; break;                           /* JEQ */
    case 0x14: take = (st & (ST_LGT | ST_EQ)) != 0; break;                /* JHE */
    case 0x15: take = (st & ST_AGT) != 0; break;                          /* JGT */
    case 0x16: take = !(st & ST_EQ); break;                               /* JNE */
    case 0x17: take = !(st & ST_C); break;                                /* JNC */
    case 0x18: take = (st & ST_C) != 0; break;                            /* JOC */
    case 0x19: take = !(st & ST_OV); break;                               /* JNO */
    case 0x1A: take = !(st & ST_LGT) && !(st & ST_EQ); break;             /* JL  */
    case 0x1B: take = (st & ST_LGT) && !(st & ST_EQ); break;              /* JH  */
    case 0x1C: take = (st & ST_OP) != 0; break;                           /* JOP */
    default: {                                                            /* SBO SBZ TB */
        unsigned bit = (cru_base(c) + (unsigned)(int)disp) & 0x0FFFu;
        unsigned sel = op >> 8;
        if (sel == 0x1D)      c->cru_out(c->ctx, (uint16_t)bit, 1);
        else if (sel == 0x1E) c->cru_out(c->ctx, (uint16_t)bit, 0);
        else {
            if (c->cru_in(c->ctx, (uint16_t)bit)) c->st |= ST_EQ;
            else c->st &= (uint16_t)~ST_EQ;
        }
        return 12;
    }
    }
    if (take) { c->pc = (uint16_t)(c->pc + 2 * (int)disp); return 10; }
    return 8;
}

/* ------------------------------------------------------ format 3/9 + CRU */

static int fmt3(tms9900 *c, uint16_t op)
{
    unsigned sel = op >> 10;             /* 8..15 */
    unsigned dreg = (op >> 6) & 15u;
    int cyc = 14;
    int byte = 0;
    unsigned count = dreg ? dreg : 16u;

    if (sel == 12 || sel == 13) byte = (count <= 8);
    uint16_t sa = ea(c, (op >> 4) & 3u, op & 15u, byte, &cyc);

    switch (sel) {
    case 8: {                            /* COC */
        uint16_t s = rdw(c, sa), d = getreg(c, dreg);
        if ((s & d) == s) c->st |= ST_EQ; else c->st &= (uint16_t)~ST_EQ;
        return cyc;
    }
    case 9: {                            /* CZC */
        uint16_t s = rdw(c, sa), d = getreg(c, dreg);
        if ((s & d) == 0) c->st |= ST_EQ; else c->st &= (uint16_t)~ST_EQ;
        return cyc;
    }
    case 10: {                           /* XOR */
        uint16_t s = rdw(c, sa), d = getreg(c, dreg);
        uint16_t r = (uint16_t)(d ^ s);
        setreg(c, dreg, r);
        lae16(c, r);
        return cyc;
    }
    case 11: {                           /* XOP */
        uint16_t vec = (uint16_t)(0x40 + 4 * dreg);
        uint16_t oldpc = c->pc;
        context_switch(c, vec, oldpc);
        setreg(c, 11, sa);
        c->st |= ST_X;
        return 36 + cyc - 14;
    }
    case 12: {                           /* LDCR */
        unsigned val = byte ? rdb(c, sa) : rdw(c, sa);
        unsigned base = cru_base(c), i;
        if (byte) lae8(c, (uint8_t)val); else lae16(c, (uint16_t)val);
        for (i = 0; i < count; i++)
            c->cru_out(c->ctx, (uint16_t)((base + i) & 0x0FFFu), (int)((val >> i) & 1u));
        return cyc - 14 + 20 + 2 * (int)count;
    }
    case 13: {                           /* STCR */
        unsigned base = cru_base(c), i, val = 0;
        for (i = 0; i < count; i++)
            if (c->cru_in(c->ctx, (uint16_t)((base + i) & 0x0FFFu))) val |= 1u << i;
        if (byte) { wrb(c, sa, (uint8_t)val); lae8(c, (uint8_t)val); }
        else      { wrw(c, sa, (uint16_t)val); lae16(c, (uint16_t)val); }
        return cyc - 14 + (count == 8 ? 44 : (count < 8 ? 42 : 58));
    }
    case 14: {                           /* MPY */
        uint32_t r = (uint32_t)getreg(c, dreg) * rdw(c, sa);
        setreg(c, dreg, (uint16_t)(r >> 16));
        setreg(c, (dreg + 1) & 15u, (uint16_t)r);
        return cyc - 14 + 52;
    }
    default: {                           /* DIV */
        uint16_t s = rdw(c, sa), hi = getreg(c, dreg);
        if (s <= hi) { c->st |= ST_OV; return cyc - 14 + 16; }
        c->st &= (uint16_t)~ST_OV;
        {
            uint32_t dividend = ((uint32_t)hi << 16) | getreg(c, (dreg + 1) & 15u);
            setreg(c, dreg, (uint16_t)(dividend / s));
            setreg(c, (dreg + 1) & 15u, (uint16_t)(dividend % s));
        }
        return cyc - 14 + 92;
    }
    }
}

/* ----------------------------------------------------------------- shifts */

static int fmt5(tms9900 *c, uint16_t op)
{
    unsigned count = (op >> 4) & 15u;
    unsigned r = op & 15u;
    int cyc = 12;
    uint16_t v = getreg(c, r), res;
    unsigned carry = 0, ov = 0, i;

    if (count == 0) {
        count = getreg(c, 0) & 15u;
        if (count == 0) count = 16;
        cyc = 20;
    }
    cyc += 2 * (int)count;

    switch ((op >> 8) & 3u) {
    case 0: {                            /* SRA */
        int32_t x = (int16_t)v;
        carry = (unsigned)((x >> (count - 1)) & 1);
        res = (uint16_t)(x >> count);
        break;
    }
    case 1:                              /* SRL */
        carry = (unsigned)((v >> (count - 1)) & 1u);
        res = (uint16_t)(count >= 16 ? 0u : (unsigned)v >> count);
        break;
    case 2: {                            /* SLA */
        uint16_t t = v;
        for (i = 0; i < count; i++) {
            uint16_t n = (uint16_t)(t << 1);
            carry = (t >> 15) & 1u;
            if ((n ^ t) & 0x8000u) ov = 1;
            t = n;
        }
        res = t;
        break;
    }
    default: {                           /* SRC */
        unsigned n = count & 15u;
        res = n ? (uint16_t)((v >> n) | (v << (16 - n))) : v;
        carry = (res >> 15) & 1u;
        break;
    }
    }
    setreg(c, r, res);
    lae16(c, res);
    c->st &= (uint16_t)~ST_C;
    if (carry) c->st |= ST_C;
    if (((op >> 8) & 3u) == 2) {
        c->st &= (uint16_t)~ST_OV;
        if (ov) c->st |= ST_OV;
    }
    return cyc;
}

/* --------------------------------------------------- format 6: one operand */

static int fmt6(tms9900 *c, uint16_t op)
{
    int cyc = 0;
    unsigned sel = (op >> 6) & 0xFu;     /* 0x0400..0x07FF -> 0..15 */
    uint16_t sa = ea(c, (op >> 4) & 3u, op & 15u, 0, &cyc);
    uint16_t v, r;

    switch (sel) {
    case 0x0: {                          /* BLWP */
        uint16_t oldpc = c->pc;
        context_switch(c, sa, oldpc);
        return cyc + 26;
    }
    case 0x1: c->pc = sa; return cyc + 8;                       /* B   */
    case 0x2: {                                                 /* X   */
        uint16_t xop = rdw(c, sa);
        return cyc + 8 + exec(c, xop);
    }
    case 0x3: c->touch(c->ctx, sa); wrw(c, sa, 0); return cyc + 10;   /* CLR */
    case 0x4:                                                   /* NEG */
        v = rdw(c, sa);
        r = sub16(c, 0, v);
        wrw(c, sa, r);
        return cyc + 12;
    case 0x5: v = rdw(c, sa); r = (uint16_t)~v; lae16(c, r); wrw(c, sa, r); return cyc + 10; /* INV */
    case 0x6: v = rdw(c, sa); r = add16(c, v, 1); wrw(c, sa, r); return cyc + 10;            /* INC */
    case 0x7: v = rdw(c, sa); r = add16(c, v, 2); wrw(c, sa, r); return cyc + 10;            /* INCT */
    case 0x8: v = rdw(c, sa); r = sub16(c, v, 1); wrw(c, sa, r); return cyc + 10;            /* DEC */
    case 0x9: v = rdw(c, sa); r = sub16(c, v, 2); wrw(c, sa, r); return cyc + 10;            /* DECT */
    case 0xA:                                                   /* BL  */
        setreg(c, 11, c->pc);
        c->pc = sa;
        return cyc + 12;
    case 0xB:                                                   /* SWPB */
        v = rdw(c, sa);
        wrw(c, sa, (uint16_t)((v >> 8) | (v << 8)));
        return cyc + 10;
    case 0xC: c->touch(c->ctx, sa); wrw(c, sa, 0xFFFF); return cyc + 10;  /* SETO */
    case 0xD:                                                   /* ABS */
        v = rdw(c, sa);
        lae16(c, v);
        c->st &= (uint16_t)~(ST_C | ST_OV);
        if (v == 0x8000u) c->st |= ST_OV;
        if (v & 0x8000u) { wrw(c, sa, (uint16_t)(0u - v)); return cyc + 14; }
        c->touch(c->ctx, sa);
        return cyc + 12;
    default:
        return cyc + 6;                                         /* illegal */
    }
}

/* ------------------------------------------------ immediates / no-operand */

static int fmt8(tms9900 *c, uint16_t op)
{
    unsigned r = op & 15u;
    uint16_t imm, v;

    switch (op & 0xFFE0u) {
    case 0x0200: imm = fetch(c); setreg(c, r, imm); lae16(c, imm); return 12;            /* LI   */
    case 0x0220: imm = fetch(c); v = getreg(c, r); setreg(c, r, add16(c, v, imm)); return 14; /* AI */
    case 0x0240: imm = fetch(c); v = (uint16_t)(getreg(c, r) & imm); setreg(c, r, v); lae16(c, v); return 14; /* ANDI */
    case 0x0260: imm = fetch(c); v = (uint16_t)(getreg(c, r) | imm); setreg(c, r, v); lae16(c, v); return 14; /* ORI */
    case 0x0280: imm = fetch(c); v = getreg(c, r); cmp16(c, v, imm); return 14;          /* CI   */
    case 0x02A0: c->touch(c->ctx, reg_addr(c, r)); setreg(c, r, c->wp); return 8;         /* STWP */
    case 0x02C0: c->touch(c->ctx, reg_addr(c, r)); setreg(c, r, c->st); return 8;         /* STST */
    case 0x02E0: imm = fetch(c); c->wp = imm; return 10;                                  /* LWPI */
    case 0x0300: imm = fetch(c); c->st = (uint16_t)((c->st & 0xFFF0u) | (imm & 0xFu)); return 16; /* LIMI */
    default:     return 6;
    }
}

static int fmt7(tms9900 *c, uint16_t op)
{
    switch (op & 0xFFE0u) {
    case 0x0340: c->idle = 1; return 12;                                     /* IDLE */
    case 0x0360: c->st &= 0xFFF0u; return 12;                                 /* RSET */
    case 0x0380:                                                              /* RTWP */
        c->st = rdw(c, (uint16_t)(c->wp + 30));
        c->pc = rdw(c, (uint16_t)(c->wp + 28));
        c->wp = rdw(c, (uint16_t)(c->wp + 26));
        return 14;
    default:     return 12;                                                   /* CKON CKOF LREX */
    }
}

static int exec(tms9900 *c, uint16_t op)
{
    if (op >= 0x4000u)  return fmt1(c, op);
    if (op >= 0x2000u)  return fmt3(c, op);
    if (op >= 0x1000u)  return fmt2(c, op);
    if (op >= 0x0C00u)  return 6;                    /* undefined */
    if (op >= 0x0800u)  return fmt5(c, op);
    if (op >= 0x0400u)  return fmt6(c, op);
    if (op >= 0x0340u)  return fmt7(c, op);
    if (op >= 0x0200u)  return fmt8(c, op);
    return 6;                                        /* undefined */
}

/* ---------------------------------------------------------------- public */

void tms9900_reset(tms9900 *c)
{
    c->idle = 0;
    c->intreq = 0;
    c->extra = 0;
    c->st = 0;
    c->wp = rdw(c, 0);
    c->pc = rdw(c, 2);
}

int tms9900_step(tms9900 *c)
{
    int cyc;
    c->extra = 0;

    if (c->intreq && (c->st & 0x000Fu) >= 1u) {          /* level 1 request */
        uint16_t oldpc = c->pc;
        c->idle = 0;
        context_switch(c, 4, oldpc);
        c->st = (uint16_t)((c->st & 0xFFF0u) | 0u);      /* mask := level - 1 */
        return 22 + (int)c->extra;
    }
    if (c->idle) return 4;

    cyc = exec(c, fetch(c));
    return cyc + (int)c->extra;
}
