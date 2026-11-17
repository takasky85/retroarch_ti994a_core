/* Unit and integration tests for the TI-99/4A core (no BIOS needed).
 *   build: see Makefile target "test"      run: ./test_ti99 <deflate-test-zip>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cpu9900.h"
#include "vdp9918.h"
#include "psg76489.h"
#include "ti99.h"
#include "zipread.h"
#include "loader.h"
#include "speech.h"
#include "osk.h"
#include "keymatrix.h"

static int g_fail = 0, g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fail++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { unsigned long _a = (unsigned long)(a), _b = (unsigned long)(b); g_checks++; \
    if (_a != _b) { g_fail++; printf("  FAIL %s:%d: %s == %s  (got 0x%lX, want 0x%lX)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

/* ===================================================================== CPU */

typedef struct { uint8_t mem[65536]; uint8_t cru[4096]; unsigned waits; } tbus;

static uint16_t t_read(void *c, uint16_t a) { tbus *b = c; return (uint16_t)((b->mem[a] << 8) | b->mem[(uint16_t)(a + 1)]); }
static void t_write(void *c, uint16_t a, uint16_t v) { tbus *b = c; b->mem[a] = (uint8_t)(v >> 8); b->mem[(uint16_t)(a + 1)] = (uint8_t)v; }
static void t_touch(void *c, uint16_t a) { (void)c; (void)a; }
static int  t_cin(void *c, uint16_t bit) { tbus *b = c; return b->cru[bit & 0xFFF]; }
static void t_cout(void *c, uint16_t bit, int v) { tbus *b = c; b->cru[bit & 0xFFF] = (uint8_t)v; }

static tbus bus;
static tms9900 cpu;
static uint16_t *asmp;

static void cpu_setup(void)
{
    memset(&bus, 0, sizeof bus);
    memset(&cpu, 0, sizeof cpu);
    cpu.ctx = &bus; cpu.read = t_read; cpu.write = t_write; cpu.touch = t_touch;
    cpu.cru_in = t_cin; cpu.cru_out = t_cout;
    cpu.wp = 0x0200; cpu.pc = 0x1000; cpu.st = 0;
    asmp = (uint16_t *)(void *)0;                 /* not used: we assemble through t_write */
}

static uint16_t apc;
static void W(uint16_t w) { t_write(&bus, apc, w); apc = (uint16_t)(apc + 2); }
static void begin(uint16_t at) { apc = at; }
static uint16_t F1(uint16_t op, unsigned ts, unsigned s, unsigned td, unsigned d)
{ return (uint16_t)(op | (td << 10) | (d << 6) | (ts << 4) | s); }
static void LI(unsigned r, uint16_t v) { W((uint16_t)(0x0200 | r)); W(v); }
static void AI(unsigned r, uint16_t v) { W((uint16_t)(0x0220 | r)); W(v); }
static uint16_t R(unsigned n) { return t_read(&bus, (uint16_t)(cpu.wp + 2 * n)); }
static void run(int n) { while (n--) tms9900_step(&cpu); }
static uint16_t M(uint16_t a) { return t_read(&bus, a); }

static void test_cpu(void)
{
    printf("CPU\n");

    /* add + flags */
    cpu_setup(); begin(0x1000);
    LI(0, 0x1234); LI(1, 0x0001); W(F1(0xA000, 0, 1, 0, 0));
    run(3);
    CHECK_EQ(R(0), 0x1235); CHECK(cpu.st & ST_LGT); CHECK(cpu.st & ST_AGT); CHECK(!(cpu.st & ST_C));

    cpu_setup(); begin(0x1000); LI(0, 0x7FFF); AI(0, 1); run(2);
    CHECK_EQ(R(0), 0x8000); CHECK(cpu.st & ST_OV); CHECK(!(cpu.st & ST_AGT)); CHECK(cpu.st & ST_LGT);

    cpu_setup(); begin(0x1000); LI(0, 0xFFFF); AI(0, 1); run(2);
    CHECK_EQ(R(0), 0); CHECK(cpu.st & ST_EQ); CHECK(cpu.st & ST_C); CHECK(!(cpu.st & ST_OV));

    /* subtract, with and without borrow: S R1,R0 -> R0 = R0 - R1 */
    cpu_setup(); begin(0x1000); LI(0, 5); LI(1, 3); W(F1(0x6000, 0, 1, 0, 0)); run(3);
    CHECK_EQ(R(0), 2); CHECK(cpu.st & ST_C); CHECK(cpu.st & ST_AGT);
    cpu_setup(); begin(0x1000); LI(0, 3); LI(1, 5); W(F1(0x6000, 0, 1, 0, 0)); run(3);
    CHECK_EQ(R(0), 0xFFFE); CHECK(!(cpu.st & ST_C)); CHECK(!(cpu.st & ST_AGT)); CHECK(cpu.st & ST_LGT);

    /* MOV with auto-increment source, symbolic and indexed destination */
    cpu_setup(); begin(0x1000);
    LI(1, 0x0300); W(F1(0xC000, 3, 1, 0, 2));
    W(F1(0xC000, 0, 2, 2, 0)); W(0x0310);            /* MOV R2,@>0310 */
    t_write(&bus, 0x0300, 0xBEEF);
    run(3);
    CHECK_EQ(R(2), 0xBEEF); CHECK_EQ(R(1), 0x0302);
    run(1); CHECK_EQ(M(0x0310), 0xBEEF);

    /* MOVB: byte lands in the addressed half, OP set by parity, flags from the byte */
    cpu_setup(); begin(0x1000);
    LI(0, 0xAB00); W(F1(0xD000, 0, 0, 2, 0)); W(0x0300);      /* MOVB R0,@>0300 */
    W(F1(0xD000, 0, 0, 2, 0)); W(0x0301);                     /* MOVB R0,@>0301 */
    run(2);
    CHECK_EQ(cpu.st & ST_OP, ST_OP); CHECK(cpu.st & ST_LGT); CHECK(!(cpu.st & ST_AGT));  /* 0xAB is negative */
    run(1);
    CHECK_EQ(M(0x0300), 0xABAB);

    /* byte indirect with autoincrement increments by one */
    cpu_setup(); begin(0x1000);
    LI(1, 0x0300); LI(2, 0x0400); W(F1(0xD000, 3, 1, 3, 2)); W(F1(0xD000, 3, 1, 3, 2));
    t_write(&bus, 0x0300, 0x1122);
    run(4);
    CHECK_EQ(R(1), 0x0302); CHECK_EQ(R(2), 0x0402); CHECK_EQ(M(0x0400), 0x1122);

    /* compare */
    cpu_setup(); begin(0x1000); LI(0, 0xFFFF); LI(1, 1); W(F1(0x8000, 0, 0, 0, 1)); run(3);   /* C R0,R1 */
    CHECK(cpu.st & ST_LGT); CHECK(!(cpu.st & ST_AGT)); CHECK(!(cpu.st & ST_EQ));
    cpu_setup(); begin(0x1000); LI(0, 5); W(0x0280); W(5); run(2);
    CHECK(cpu.st & ST_EQ);

    /* SOC / SZC */
    cpu_setup(); begin(0x1000); LI(0, 0x00F0); LI(1, 0x000F); W(F1(0xE000, 0, 0, 0, 1)); run(3);
    CHECK_EQ(R(1), 0x00FF);
    cpu_setup(); begin(0x1000); LI(0, 0x00F0); LI(1, 0x00FF); W(F1(0x4000, 0, 0, 0, 1)); run(3);
    CHECK_EQ(R(1), 0x000F);

    /* COC / CZC / XOR */
    cpu_setup(); begin(0x1000); LI(0, 0x00F0); LI(1, 0x00FF);
    W((uint16_t)(0x2000 | (1 << 6) | 0)); run(3);               /* COC R0,R1 */
    CHECK(cpu.st & ST_EQ);
    cpu_setup(); begin(0x1000); LI(0, 0x00F0); LI(1, 0x000F);
    W((uint16_t)(0x2400 | (1 << 6) | 0)); run(3);               /* CZC R0,R1 */
    CHECK(cpu.st & ST_EQ);
    cpu_setup(); begin(0x1000); LI(0, 0xFF00); LI(1, 0x0FF0);
    W((uint16_t)(0x2800 | (1 << 6) | 0)); run(3);               /* XOR R0,R1 */
    CHECK_EQ(R(1), 0xF0F0);

    /* MPY and DIV */
    cpu_setup(); begin(0x1000); LI(1, 0x00FF); LI(3, 0x0100);
    W((uint16_t)(0x3800 | (1 << 6) | 3)); run(3);               /* MPY R3,R1 */
    CHECK_EQ(R(1), 0x0000); CHECK_EQ(R(2), 0xFF00);
    cpu_setup(); begin(0x1000); LI(1, 1); LI(2, 0); LI(3, 3);
    W((uint16_t)(0x3C00 | (1 << 6) | 3)); run(4);               /* DIV R3,R1 */
    CHECK_EQ(R(1), 0x5555); CHECK_EQ(R(2), 1); CHECK(!(cpu.st & ST_OV));
    cpu_setup(); begin(0x1000); LI(1, 5); LI(2, 0); LI(3, 3);
    W((uint16_t)(0x3C00 | (1 << 6) | 3)); run(4);               /* overflow: divisor <= high word */
    CHECK(cpu.st & ST_OV); CHECK_EQ(R(1), 5);

    /* shifts */
    cpu_setup(); begin(0x1000); LI(0, 0x8001); W(0x0810); run(2);             /* SRA R0,1 */
    CHECK_EQ(R(0), 0xC000); CHECK(cpu.st & ST_C);
    cpu_setup(); begin(0x1000); LI(0, 0x8001); W(0x0910); run(2);             /* SRL R0,1 */
    CHECK_EQ(R(0), 0x4000); CHECK(cpu.st & ST_C);
    cpu_setup(); begin(0x1000); LI(0, 0x4000); W(0x0A10); run(2);             /* SLA R0,1 */
    CHECK_EQ(R(0), 0x8000); CHECK(cpu.st & ST_OV); CHECK(!(cpu.st & ST_C));
    cpu_setup(); begin(0x1000); LI(0, 0x1234); W(0x0B40); run(2);             /* SRC R0,4 */
    CHECK_EQ(R(0), 0x4123);
    cpu_setup(); begin(0x1000); LI(1, 0x0001); LI(0, 0x0004); W(0x0A01); run(3);   /* SLA R1,R0 (count in R0) */
    CHECK_EQ(R(1), 0x0010);

    /* loop with DEC / JNE */
    cpu_setup(); begin(0x1000);
    LI(0, 0); LI(1, 10);
    W(F1(0xA000, 0, 1, 0, 0)); W(0x0601); W(0x16FD);           /* loop: A R1,R0 ; DEC R1 ; JNE loop */
    run(2 + 30);
    CHECK_EQ(R(0), 55); CHECK_EQ(R(1), 0);

    /* BL and B *R11 */
    cpu_setup(); begin(0x1000);
    W(0x06A0); W(0x1010);                                      /* BL @>1010 */
    LI(5, 0x1111);                                             /* 1004 */
    begin(0x1010); LI(4, 0x2222); W(0x045B);                   /* B *R11 */
    run(3);
    CHECK_EQ(R(4), 0x2222); CHECK_EQ(cpu.pc, 0x1004);
    run(1); CHECK_EQ(R(5), 0x1111);

    /* BLWP / RTWP context switch */
    cpu_setup(); begin(0x1000);
    LI(1, 0xAAAA); W(0x0420); W(0x0400);                       /* BLWP @>0400 */
    begin(0x0400); W(0x0500); W(0x2000);                       /* WP=0500 PC=2000 */
    begin(0x2000); LI(1, 0x5555); W(0x0380);                   /* RTWP */
    cpu.st = 0x1234 & ~0xF; run(2);
    {
        uint16_t oldst = cpu.st;
        CHECK_EQ(cpu.wp, 0x0500); CHECK_EQ(cpu.pc, 0x2000);
        CHECK_EQ(M(0x0500 + 26), 0x0200);                      /* R13 = old WP */
        CHECK_EQ(M(0x0500 + 28), 0x1008);                      /* R14 = old PC */
        CHECK_EQ(M(0x0500 + 30), oldst);                       /* R15 = old ST */
        run(2);
        CHECK_EQ(cpu.wp, 0x0200); CHECK_EQ(cpu.pc, 0x1008);
        CHECK_EQ(R(1), 0xAAAA);
    }

    /* X instruction */
    cpu_setup(); begin(0x1000); LI(0, 5); LI(5, 0x0580); W(0x0485); run(3);   /* X R5 -> INC R0 */
    CHECK_EQ(R(0), 6);

    /* one-operand group */
    cpu_setup(); begin(0x1000); LI(0, 0x8000); W(0x0740); run(2);             /* ABS R0 */
    CHECK_EQ(R(0), 0x8000); CHECK(cpu.st & ST_OV);
    cpu_setup(); begin(0x1000); LI(0, 0xFFFE); W(0x0740); run(2); CHECK_EQ(R(0), 2);
    cpu_setup(); begin(0x1000); LI(0, 1); W(0x0500); run(2); CHECK_EQ(R(0), 0xFFFF);        /* NEG */
    cpu_setup(); begin(0x1000); LI(0, 0x00FF); W(0x0540); run(2); CHECK_EQ(R(0), 0xFF00);   /* INV */
    cpu_setup(); begin(0x1000); LI(0, 0x1234); W(0x06C0); run(2); CHECK_EQ(R(0), 0x3412);   /* SWPB */
    cpu_setup(); begin(0x1000); LI(0, 0x1234); W(0x04C0); run(2); CHECK_EQ(R(0), 0);        /* CLR */
    cpu_setup(); begin(0x1000); W(0x0700); run(1); CHECK_EQ(R(0), 0xFFFF);                  /* SETO */
    cpu_setup(); begin(0x1000); LI(0, 10); W(0x05C0); W(0x0640); run(3); CHECK_EQ(R(0), 10);/* INCT, DECT */

    /* immediates, STWP / STST / LWPI / LIMI */
    cpu_setup(); begin(0x1000); W(0x02A0 | 1); W(0x02E0); W(0x0300); W(0x0300); W(0x0003); run(1);
    CHECK_EQ(R(1), 0x0200);
    run(1); CHECK_EQ(cpu.wp, 0x0300);
    run(1); CHECK_EQ(cpu.st & 0xF, 3);
    cpu_setup(); begin(0x1000); LI(0, 0x00FF); W(0x0240); W(0x0F0F); run(2); CHECK_EQ(R(0), 0x000F);
    cpu_setup(); begin(0x1000); LI(0, 0x00F0); W(0x0260); W(0x000F); run(2); CHECK_EQ(R(0), 0x00FF);

    /* conditional jumps */
    cpu_setup(); begin(0x1000); LI(0, 1); W(0x0280); W(2); W(0x1501); LI(5, 1); LI(6, 2);   /* CI R0,2 ; JGT +1 (not taken) */
    run(3); CHECK_EQ(cpu.pc, 0x100A);
    cpu_setup(); begin(0x1000); LI(0, 5); W(0x0280); W(2); W(0x1501); LI(5, 1); LI(6, 2);   /* taken */
    run(3); CHECK_EQ(cpu.pc, 0x100C);

    /* CRU: SBO/SBZ/TB, LDCR/STCR */
    cpu_setup(); begin(0x1000);
    LI(12, 0x0040); W(0x1D03); W(0x1F03); W(0x1E03); W(0x1F03);
    run(3); CHECK_EQ(bus.cru[0x23], 1); CHECK(cpu.st & ST_EQ);
    run(2); CHECK_EQ(bus.cru[0x23], 0); CHECK(!(cpu.st & ST_EQ));
    cpu_setup(); begin(0x1000);
    LI(12, 0x0100); LI(0, 0xA500); W((uint16_t)(0x3000 | (8 << 6) | 0));        /* LDCR R0,8 */
    LI(12, 0x0100); W((uint16_t)(0x3400 | (8 << 6) | 1));                       /* STCR R1,8 */
    run(5);
    CHECK_EQ(bus.cru[0x80], 1); CHECK_EQ(bus.cru[0x81], 0); CHECK_EQ(bus.cru[0x82], 1); CHECK_EQ(bus.cru[0x87], 1);
    CHECK_EQ(R(1) >> 8, 0xA5);

    /* interrupt, level 1 */
    cpu_setup(); begin(0x1000); W(0x1000);                      /* JMP $ */
    t_write(&bus, 4, 0x0600); t_write(&bus, 6, 0x3000);
    cpu.st = 0x0002; cpu.intreq = 1;
    run(1);
    CHECK_EQ(cpu.wp, 0x0600); CHECK_EQ(cpu.pc, 0x3000); CHECK_EQ(cpu.st & 0xF, 0);
    CHECK_EQ(M(0x0600 + 30), 0x0002);
    cpu_setup(); begin(0x1000); W(0x1000); cpu.st = 0; cpu.intreq = 1;   /* masked */
    run(1); CHECK_EQ(cpu.wp, 0x0200);

    /* clock counts (workspace in zero-wait RAM) */
    cpu_setup(); begin(0x1000); W(F1(0xC000, 0, 0, 0, 1)); W(0x0000);
    CHECK_EQ(tms9900_step(&cpu), 14);
    cpu_setup(); begin(0x1000); LI(0, 1);
    CHECK_EQ(tms9900_step(&cpu), 12);
    cpu_setup(); begin(0x1000); W(0x1000);
    CHECK_EQ(tms9900_step(&cpu), 10);
}

/* ===================================================================== VDP */

static void vctrl(vdp9918 *v, uint8_t a, uint8_t b) { vdp_write_ctrl(v, a); vdp_write_ctrl(v, b); }
static void vreg(vdp9918 *v, int r, uint8_t val) { vctrl(v, val, (uint8_t)(0x80 | r)); }
static void vaddr(vdp9918 *v, unsigned a) { vctrl(v, (uint8_t)a, (uint8_t)(0x40 | ((a >> 8) & 0x3F))); }
static void vfill(vdp9918 *v, unsigned a, uint8_t val, unsigned n) { vaddr(v, a); while (n--) vdp_write_data(v, val); }
#define PAL(i) vdp9918_palette[i]

static void test_vdp(void)
{
    static vdp9918 v;
    uint32_t line[256];
    int i;
    printf("VDP\n");

    vdp_reset(&v);
    /* address / data port behaviour */
    vaddr(&v, 0x1234);
    for (i = 0; i < 4; i++) vdp_write_data(&v, (uint8_t)(0x10 + i));
    CHECK_EQ(v.vram[0x1234], 0x10); CHECK_EQ(v.vram[0x1237], 0x13);
    vctrl(&v, 0x34, 0x12);                                  /* read set-up (bit 6 clear) */
    CHECK_EQ(vdp_read_data(&v), 0x10); CHECK_EQ(vdp_read_data(&v), 0x11); CHECK_EQ(vdp_read_data(&v), 0x12);
    CHECK_EQ(v.addr, 0x1238);

    /* status / interrupt line */
    vreg(&v, 1, 0xE0);
    CHECK(!vdp_int_line(&v));
    vdp_set_frame_flag(&v);
    CHECK(vdp_int_line(&v));
    CHECK_EQ(vdp_read_status(&v) & 0x80, 0x80);
    CHECK(!vdp_int_line(&v)); CHECK_EQ(vdp_read_status(&v) & 0x80, 0);
    vreg(&v, 1, 0xC0); vdp_set_frame_flag(&v); CHECK(!vdp_int_line(&v));    /* IE off */

    /* Graphics I */
    vdp_reset(&v); memset(v.vram, 0, sizeof v.vram);
    vreg(&v, 0, 0x00); vreg(&v, 1, 0xC0); vreg(&v, 2, 0x00); vreg(&v, 3, 0x0E); vreg(&v, 4, 0x01); vreg(&v, 7, 0x01);
    vfill(&v, 0x000, 0x01, 1);                              /* name[0] = 1 */
    vfill(&v, 0x808, 0xFF, 8);                              /* pattern for char 1 */
    vfill(&v, 0x380, 0xF4, 1);                              /* colour group 0 : white on dark blue */
    vdp_render_line(&v, 0, line, 4);
    CHECK_EQ(line[0], PAL(15)); CHECK_EQ(line[7], PAL(15)); CHECK_EQ(line[8], PAL(4));
    vdp_render_line(&v, 7, line, 4); CHECK_EQ(line[3], PAL(15));
    vdp_render_line(&v, 8, line, 4); CHECK_EQ(line[3], PAL(4));       /* next row: char 0 -> bg */

    /* blank display shows only the backdrop */
    vreg(&v, 1, 0x80); vdp_render_line(&v, 0, line, 4); CHECK_EQ(line[0], PAL(1)); vreg(&v, 1, 0xC0);

    /* sprites: size, position, colour, early clock, magnification */
    vreg(&v, 5, 0x20); vreg(&v, 6, 0x03);                   /* SAT at >1000, patterns at >1800 */
    vfill(&v, 0x1000, 0xD0, 4);
    vaddr(&v, 0x1000); vdp_write_data(&v, 9); vdp_write_data(&v, 100); vdp_write_data(&v, 2); vdp_write_data(&v, 8);
    vdp_write_data(&v, 0xD0);
    vfill(&v, 0x1810, 0x80, 8);
    vdp_render_line(&v, 9, line, 4);
    CHECK(line[100] != PAL(8));                              /* sprite starts on line Y+1 */
    vdp_render_line(&v, 10, line, 4); CHECK_EQ(line[100], PAL(8)); CHECK(line[101] != PAL(8));
    vdp_render_line(&v, 17, line, 4); CHECK_EQ(line[100], PAL(8));
    vdp_render_line(&v, 18, line, 4); CHECK(line[100] != PAL(8));
    vreg(&v, 1, 0xC1);                                       /* magnified: 16 lines, 2 px wide */
    vdp_render_line(&v, 25, line, 4); CHECK_EQ(line[100], PAL(8)); CHECK_EQ(line[101], PAL(8)); CHECK(line[102] != PAL(8));
    vreg(&v, 1, 0xC0);
    vaddr(&v, 0x1003); vdp_write_data(&v, 0x88);             /* early clock bit: x - 32 */
    vdp_render_line(&v, 10, line, 4); CHECK_EQ(line[68], PAL(8)); CHECK(line[100] != PAL(8));
    vaddr(&v, 0x1003); vdp_write_data(&v, 0x08);

    /* collision and fifth-sprite flag */
    vdp_read_status(&v);
    vaddr(&v, 0x1004); vdp_write_data(&v, 9); vdp_write_data(&v, 103); vdp_write_data(&v, 2); vdp_write_data(&v, 5);
    vfill(&v, 0x1810, 0xFF, 8);
    vdp_render_line(&v, 10, line, 4);
    CHECK_EQ(v.status & 0x20, 0x20);
    vdp_read_status(&v);
    vfill(&v, 0x1000, 0xD0, 1);
    vaddr(&v, 0x1000);
    for (i = 0; i < 6; i++) { vdp_write_data(&v, 9); vdp_write_data(&v, (uint8_t)(i * 20)); vdp_write_data(&v, 2); vdp_write_data(&v, 5); }
    vdp_write_data(&v, 0xD0);
    vdp_render_line(&v, 10, line, 4);
    CHECK_EQ(v.status & 0x5F, 0x40 | 4);                     /* 5S set, sprite number 4 */
    vdp_read_status(&v);
    vdp_render_line(&v, 10, line, 32);
    CHECK_EQ(v.status & 0x40, 0);                            /* unlimited mode: no flag */
    CHECK_EQ(line[100], PAL(5));

    /* transparent (colour 0) sprites do not hide lower-priority sprites */
    vdp_reset(&v); memset(v.vram, 0, sizeof v.vram);
    vreg(&v, 1, 0xC0); vreg(&v, 5, 0x20); vreg(&v, 6, 0x03); vreg(&v, 7, 0x01);
    vaddr(&v, 0x1000);
    vdp_write_data(&v, 9); vdp_write_data(&v, 50); vdp_write_data(&v, 2); vdp_write_data(&v, 0);
    vdp_write_data(&v, 9); vdp_write_data(&v, 50); vdp_write_data(&v, 2); vdp_write_data(&v, 6);
    vdp_write_data(&v, 0xD0);
    vfill(&v, 0x1810, 0xFF, 8);
    vdp_render_line(&v, 10, line, 4);
    CHECK_EQ(line[50], PAL(6));

    /* Graphics II */
    vdp_reset(&v); memset(v.vram, 0, sizeof v.vram);
    vreg(&v, 0, 0x02); vreg(&v, 1, 0xC0); vreg(&v, 2, 0x0E); vreg(&v, 3, 0xFF); vreg(&v, 4, 0x03); vreg(&v, 7, 0x01);
    /* name table at >3800, patterns >0000, colours >2000 */
    vfill(&v, 0x3800, 0x00, 768);
    vfill(&v, 0x0000, 0xF0, 8); vfill(&v, 0x2000, 0x2A, 8);   /* tile 0 rows: 4 left px set; fg green(2) bg gray(14)... */
    vdp_render_line(&v, 3, line, 4);
    CHECK_EQ(line[0], PAL(2)); CHECK_EQ(line[3], PAL(2)); CHECK_EQ(line[4], PAL(10));
    /* third 2 uses separate pattern data */
    vfill(&v, 0x0800 + 0, 0x0F, 8); vfill(&v, 0x2800, 0x61, 8);    /* tile 0 of the middle third */
    vaddr(&v, 0x3800 + 8 * 32); vdp_write_data(&v, 0);
    vdp_render_line(&v, 64 + 2, line, 4);
    CHECK_EQ(line[0], PAL(1)); CHECK_EQ(line[7], PAL(6));

    /* Text mode */
    vdp_reset(&v); memset(v.vram, 0, sizeof v.vram);
    vreg(&v, 0, 0x00); vreg(&v, 1, 0xD0); vreg(&v, 2, 0x00); vreg(&v, 4, 0x01); vreg(&v, 7, 0xF4);
    vfill(&v, 0x000, 0x01, 1); vfill(&v, 0x808, 0xFC, 8);
    vdp_render_line(&v, 0, line, 4);
    CHECK_EQ(line[0], PAL(4));                               /* left border = backdrop (bg) */
    CHECK_EQ(line[8], PAL(15)); CHECK_EQ(line[13], PAL(15)); CHECK_EQ(line[14], PAL(4));

    /* Multicolor */
    vdp_reset(&v); memset(v.vram, 0, sizeof v.vram);
    vreg(&v, 0, 0x00); vreg(&v, 1, 0xCC); vreg(&v, 2, 0x00); vreg(&v, 4, 0x01); vreg(&v, 7, 0x01);
    vfill(&v, 0x000, 0x00, 1);
    vaddr(&v, 0x800); vdp_write_data(&v, 0x2B); vdp_write_data(&v, 0x4F);
    vdp_render_line(&v, 0, line, 4); CHECK_EQ(line[0], PAL(2)); CHECK_EQ(line[3], PAL(2)); CHECK_EQ(line[4], PAL(11));
    vdp_render_line(&v, 4, line, 4); CHECK_EQ(line[0], PAL(4)); CHECK_EQ(line[7], PAL(15));
}

/* ===================================================================== PSG */

static void test_psg(void)
{
    static psg76489 p;
    int i, crossings = 0, prev = 0;
    double rms = 0;
    printf("PSG\n");
    psg_init(&p, 48000);
    psg_write(&p, 0x80 | 0x0E); psg_write(&p, 0x0F);        /* channel 0 tone = 0xFE: ~440 Hz */
    psg_write(&p, 0x90);                                    /* volume 0 (loudest) */
    for (i = 0; i < 48000; i++) {
        int s = psg_sample(&p);
        if (s > 0 && prev <= 0) crossings++;
        prev = s;
        rms += (double)s * s;
    }
    CHECK(crossings >= 436 && crossings <= 444);
    CHECK(sqrt(rms / 48000.0) > 1000.0);
    psg_write(&p, 0x9F);                                    /* silence */
    for (i = 0; i < 100; i++) psg_sample(&p);
    CHECK_EQ(psg_sample(&p), 0);
    psg_write(&p, 0xE4); psg_write(&p, 0xF0);               /* white noise, fastest rate, loud */
    {
        int changes = 0, last = 0;
        for (i = 0; i < 4800; i++) { int s = psg_sample(&p); if (s != last) changes++; last = s; }
        CHECK(changes > 50);
    }
}

/* ============================================================ zip/inflate */

static uint8_t gen_a(size_t i) { return (uint8_t)(i * 7 + 3); }
static uint8_t gen_c(size_t i) { static uint32_t s; if (i == 0) s = 12345; s = s * 1103515245u + 12345u; return (uint8_t)(s >> 16); }

static void test_zip(const char *zipfile)
{
    size_t n = 0, i;
    uint8_t *data;
    zip_archive z;
    printf("ZIP/inflate\n");
    if (!zipfile) { printf("  (no test zip given, skipped)\n"); return; }
    data = loader_read_file(zipfile, &n);
    CHECK(data != NULL);
    if (!data) return;
    CHECK(zip_looks_like_zip(data, n));
    CHECK_EQ(zip_open(&z, data, n), 0);
    CHECK_EQ(z.count, 4);
    {
        int ia = zip_find(&z, "A.BIN"), ib = zip_find(&z, "dir/b.txt"), ic = zip_find(&z, "c.bin"), id = zip_find(&z, "empty.dat");
        size_t sz;
        uint8_t *d;
        CHECK(ia >= 0 && ib >= 0 && ic >= 0 && id >= 0);
        d = zip_extract(&z, ia, &sz);
        CHECK(d != NULL); CHECK_EQ(sz, 20000);
        if (d) { int ok = 1; for (i = 0; i < sz; i++) if (d[i] != gen_a(i)) { ok = 0; break; } CHECK(ok); free(d); }
        d = zip_extract(&z, ib, &sz);
        CHECK(d != NULL);
        if (d) { CHECK(sz > 1000); CHECK(strncmp((char *)d, "The quick brown fox", 19) == 0); free(d); }
        d = zip_extract(&z, ic, &sz);
        CHECK(d != NULL); CHECK_EQ(sz, 30000);
        if (d) { int ok = 1; for (i = 0; i < sz; i++) if (d[i] != gen_c(i)) { ok = 0; break; } CHECK(ok); free(d); }
        d = zip_extract(&z, id, &sz);
        CHECK(d != NULL); CHECK_EQ(sz, 0); free(d);
    }
    zip_close(&z);
    free(data);
}

/* ============================================ loader (BIOS + cartridges) */

/* build a stored (method 0) zip in memory */
typedef struct { const char *name; const uint8_t *data; uint32_t size; } zfile;

static uint8_t *make_zip(const zfile *f, int n, size_t *out_size)
{
    size_t cap = 4096, len = 0, cd_start;
    uint8_t *b = malloc(cap);
    uint32_t offs[16];
    int i;
#define PUT8(v)  do { if (len + 1 > cap) { cap *= 2; b = realloc(b, cap); } b[len++] = (uint8_t)(v); } while (0)
#define PUT16(v) do { PUT8((v) & 255); PUT8(((v) >> 8) & 255); } while (0)
#define PUT32(v) do { PUT16((v) & 0xFFFF); PUT16(((v) >> 16) & 0xFFFF); } while (0)
    for (i = 0; i < n; i++) {
        uint32_t crc = crc32_calc(f[i].data, f[i].size);
        size_t k;
        offs[i] = (uint32_t)len;
        PUT32(0x04034B50u); PUT16(20); PUT16(0); PUT16(0); PUT16(0); PUT16(0);
        PUT32(crc); PUT32(f[i].size); PUT32(f[i].size); PUT16(strlen(f[i].name)); PUT16(0);
        for (k = 0; f[i].name[k]; k++) PUT8(f[i].name[k]);
        for (k = 0; k < f[i].size; k++) { PUT8(f[i].data[k]); }
    }
    cd_start = len;
    for (i = 0; i < n; i++) {
        uint32_t crc = crc32_calc(f[i].data, f[i].size);
        size_t k;
        PUT32(0x02014B50u); PUT16(20); PUT16(20); PUT16(0); PUT16(0); PUT16(0); PUT16(0);
        PUT32(crc); PUT32(f[i].size); PUT32(f[i].size); PUT16(strlen(f[i].name)); PUT16(0); PUT16(0);
        PUT16(0); PUT16(0); PUT32(0); PUT32(offs[i]);
        for (k = 0; f[i].name[k]; k++) PUT8(f[i].name[k]);
    }
    {
        size_t cd_size = len - cd_start;
        PUT32(0x06054B50u); PUT16(0); PUT16(0); PUT16(n); PUT16(n); PUT32((uint32_t)cd_size); PUT32((uint32_t)cd_start); PUT16(0);
    }
    *out_size = len;
    return b;
}

static void write_file(const char *path, const void *d, size_t n) { FILE *f = fopen(path, "wb"); if (f) { fwrite(d, 1, n, f); fclose(f); } }

static void test_loader(void)
{
    static uint8_t hi[0x1000], lo[0x1000], g0[0x1800], g1[0x1800], g2[0x1800];
    static uint8_t rom[0x2000], grom[0x6000];
    char err[256];
    size_t zs, i;
    uint8_t *z;
    printf("Loader\n");

    for (i = 0; i < 0x1000; i++) { hi[i] = (uint8_t)(i & 0xFF); lo[i] = (uint8_t)(~i & 0xFF); }
    for (i = 0; i < 0x1800; i++) { g0[i] = (uint8_t)(i * 3); g1[i] = (uint8_t)(i * 5 + 1); g2[i] = (uint8_t)(i * 11 + 2); }
    {
        zfile f[5] = {
            { "994a_grom0.u500", g0, 0x1800 }, { "994a_grom1.u501", g1, 0x1800 }, { "994a_grom2.u502", g2, 0x1800 },
            { "994a_rom_hb.u610", hi, 0x1000 }, { "994a_rom_lb.u611", lo, 0x1000 } };
        z = make_zip(f, 5, &zs);
    }
    CHECK_EQ(loader_parse_console_zip(z, zs, rom, grom, err, sizeof err), 0);
    CHECK_EQ(rom[0], hi[0]); CHECK_EQ(rom[1], lo[0]); CHECK_EQ(rom[2], hi[1]); CHECK_EQ(rom[0x1FFF], lo[0xFFF]);
    CHECK_EQ(grom[5], g0[5]); CHECK_EQ(grom[0x2000 + 7], g1[7]); CHECK_EQ(grom[0x4000 + 0x17FF], g2[0x17FF]);
    free(z);
    {   /* missing GROM -> error message */
        zfile f[2] = { { "994a_rom_hb.u610", hi, 0x1000 }, { "994a_rom_lb.u611", lo, 0x1000 } };
        z = make_zip(f, 2, &zs);
        CHECK(loader_parse_console_zip(z, zs, rom, grom, err, sizeof err) != 0);
        CHECK(strstr(err, "GROM") != NULL);
        free(z);
    }

    /* softlist-driven cartridge */
    {
        static uint8_t cr[0x2000], gA[0x2000], gB[0x2000], xb1[0x3000];
        const char *xml =
            "<?xml version=\"1.0\"?>\n<softwarelist name=\"ti99_cart\">\n"
            "<software name=\"testcart\"><description>Test</description><year>1982</year>\n"
            "  <part name=\"cart\" interface=\"ti99_cart\">\n"
            "    <feature name=\"pcb\" value=\"standard\" />\n"
            "    <dataarea name=\"rom_socket\" size=\"0x2000\"><rom name=\"testcartc.bin\" size=\"0x2000\" offset=\"0x0000\" /></dataarea>\n"
            "    <dataarea name=\"grom_socket\" size=\"0x6000\">\n"
            "      <rom name=\"testcartg.bin\" size=\"0x2000\" offset=\"0x0000\" />\n"
            "      <rom name=\"testcartg2.bin\" size=\"0x2000\" offset=\"0x2000\" />\n"
            "    </dataarea>\n  </part></software>\n"
            "<software name=\"xbtest\"><part name=\"cart\" interface=\"ti99_cart\">\n"
            "  <feature name=\"pcb\" value=\"paged12k\" /><dataarea name=\"rom_socket\" size=\"0x3000\">\n"
            "  <rom name=\"xb.bin\" size=\"0x3000\" offset=\"0\" /></dataarea></part></software>\n"
            "<software name=\"weird\"><part name=\"cart\" interface=\"ti99_cart\">\n"
            "  <feature name=\"pcb\" value=\"super\" /><dataarea name=\"ram_socket\" size=\"0x8000\"/></part></software>\n"
            "</softwarelist>\n";
        loaded_cart lc;
        zfile f[3];
        char sysdir[] = "/tmp/ti99_test_sys";
        char cmd[256];
        for (i = 0; i < 0x2000; i++) { cr[i] = (uint8_t)(i + 1); gA[i] = (uint8_t)(i + 2); gB[i] = (uint8_t)(i + 3); }
        for (i = 0; i < 0x3000; i++) xb1[i] = (uint8_t)(i ^ 0x5A);
        snprintf(cmd, sizeof cmd, "mkdir -p %s/mame/hash", sysdir); if (system(cmd)) return;
        write_file("/tmp/ti99_test_sys/mame/hash/ti99_cart.xml", xml, strlen(xml));

        f[0].name = "testcartc.bin"; f[0].data = cr; f[0].size = 0x2000;
        f[1].name = "testcartg.bin"; f[1].data = gA; f[1].size = 0x2000;
        f[2].name = "testcartg2.bin"; f[2].data = gB; f[2].size = 0x2000;
        z = make_zip(f, 3, &zs);
        CHECK_EQ(loader_load_cart("/games/TestCart.zip", z, zs, sysdir, &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.type, CART_STANDARD); CHECK_EQ(lc.desc.rom_size, 0x2000); CHECK_EQ(lc.desc.ngrom, 2);
        CHECK_EQ(lc.desc.rom[5], cr[5]);
        CHECK_EQ(lc.desc.grom[0].addr, 0x6000); CHECK_EQ(lc.desc.grom[1].addr, 0x8000);
        CHECK_EQ(lc.desc.grom[1].data[9], gB[9]);
        CHECK(strstr(lc.note, "softlist") != NULL);
        loaded_cart_free(&lc); free(z);

        f[0].name = "xb.bin"; f[0].data = xb1; f[0].size = 0x3000;
        z = make_zip(f, 1, &zs);
        CHECK_EQ(loader_load_cart("xbtest.zip", z, zs, sysdir, &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.type, CART_PAGED12K); CHECK_EQ(lc.desc.rom_size, 0x3000);
        loaded_cart_free(&lc);
        CHECK(loader_load_cart("weird.zip", z, zs, sysdir, &lc, err, sizeof err) != 0);   /* name matches 'weird' but file missing */
        free(z);

        /* unknown cart type reports a clear error */
        f[0].name = "weird.bin"; f[0].data = xb1; f[0].size = 16;
        z = make_zip(f, 1, &zs);
        CHECK(loader_load_cart("weird.zip", z, zs, sysdir, &lc, err, sizeof err) != 0);
        CHECK(strstr(err, "not supported") != NULL);
        free(z);

        /* not in the softlist: heuristic by file suffix */
        f[0].name = "mygamec.bin"; f[0].data = cr; f[0].size = 0x2000;
        f[1].name = "mygameg.bin"; f[1].data = gA; f[1].size = 0x2000;
        z = make_zip(f, 2, &zs);
        CHECK_EQ(loader_load_cart("mygame.zip", z, zs, sysdir, &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.rom_size, 0x2000); CHECK_EQ(lc.desc.ngrom, 1); CHECK(strstr(lc.note, "guessed") != NULL);
        loaded_cart_free(&lc); free(z);

        /* RPK */
        {
            const char *layout =
                "<?xml version=\"1.0\" encoding=\"utf-8\"?><romset version=\"1.0\"><resources>"
                "<rom id=\"romimage1\" file=\"exc.bin\"/><rom id=\"romimage2\" file=\"exd.bin\"/><rom id=\"gromimage\" file=\"exg.bin\"/>"
                "</resources><configuration><pcb type=\"paged\"><socket id=\"rom_socket\" uses=\"romimage1\"/>"
                "<socket id=\"rom2_socket\" uses=\"romimage2\"/><socket id=\"grom_socket\" uses=\"gromimage\"/></pcb></configuration></romset>";
            zfile r[4] = { { "layout.xml", (const uint8_t *)layout, (uint32_t)strlen(layout) },
                           { "exc.bin", cr, 0x2000 }, { "exd.bin", gB, 0x2000 }, { "exg.bin", gA, 0x2000 } };
            z = make_zip(r, 4, &zs);
            CHECK_EQ(loader_load_cart("/x/ext.rpk", z, zs, sysdir, &lc, err, sizeof err), 0);
            CHECK_EQ(lc.desc.type, CART_PAGED16K); CHECK_EQ(lc.desc.rom_size, 0x4000);
            CHECK_EQ(lc.desc.rom[0x2000 + 4], gB[4]); CHECK_EQ(lc.desc.ngrom, 1);
            loaded_cart_free(&lc); free(z);
        }
    }
}

/* ======================================== whole machine with a synthetic ROM */

static uint8_t srom[0x2000], sgrom[0x6000];
static uint16_t sp;
static void SW(uint16_t w) { srom[sp] = (uint8_t)(w >> 8); srom[sp + 1u] = (uint8_t)w; sp = (uint16_t)(sp + 2); }
static void SLI(unsigned r, uint16_t v) { SW((uint16_t)(0x0200 | r)); SW(v); }
static void VDPREG(int reg, uint8_t val)
{   /* uses R1 = >8C02 */
    SLI(0, (uint16_t)(val << 8)); SW(F1(0xD000, 0, 0, 1, 1));
    SLI(0, (uint16_t)((0x80 | reg) << 8)); SW(F1(0xD000, 0, 0, 1, 1));
}
static void VDPADDR(unsigned a)
{
    SLI(0, (uint16_t)((a & 0xFF) << 8)); SW(F1(0xD000, 0, 0, 1, 1));
    SLI(0, (uint16_t)((0x4000 | a) & 0xFF00)); SW(F1(0xD000, 0, 0, 1, 1));
}
static void VDPDATA(uint8_t b) { SLI(0, (uint16_t)(b << 8)); SW(F1(0xD000, 0, 0, 1, 2)); }   /* MOVB R0,*R2 */

static void build_synth_rom(void)
{
    int i;
    memset(srom, 0, sizeof srom); memset(sgrom, 0, sizeof sgrom);
    sp = 0; SW(0x83E0); SW(0x0020);                        /* reset vector */
    SW(0x83C0); SW(0x0100);                                /* level 1 interrupt vector */
    sp = 0x20;
    SW(0x02E0); SW(0x83E0);                                /* LWPI >83E0 */
    SLI(1, 0x8C02); SLI(2, 0x8C00);
    VDPREG(0, 0x00); VDPREG(1, 0xE0); VDPREG(2, 0x00); VDPREG(3, 0x0E); VDPREG(4, 0x01); VDPREG(7, 0x04);
    VDPADDR(0x0800 + 8 * 0x41);
    VDPDATA(0x3C); VDPDATA(0x66); VDPDATA(0x66); VDPDATA(0x7E); VDPDATA(0x66); VDPDATA(0x66); VDPDATA(0x66); VDPDATA(0x00);
    VDPADDR(0x0388); VDPDATA(0xF1);
    VDPADDR(0x0000); VDPDATA(0x41); VDPDATA(0x41);
    SLI(12, 0); SW(0x1D02);                                /* SBO 2: unmask VDP interrupt in the 9901 */
    SW(0x0300); SW(0x0002);                                /* LIMI 2 */
    SW(0x10FF);                                            /* JMP $ */
    sp = 0x100;
    SLI(1, 0x8802);
    SW(F1(0xD000, 1, 1, 0, 5));                            /* MOVB *R1,R5 */
    SW(0x0580 | 0x20); SW(0x8320);                         /* INC @>8320 */
    SW(0x0380);                                            /* RTWP */
    for (i = 0; i < 0x6000; i++) sgrom[i] = (uint8_t)(i * 7);
}

static uint32_t fb_hash(const uint32_t *fb)
{
    uint32_t h = 2166136261u; int i;
    for (i = 0; i < VDP_WIDTH * VDP_HEIGHT; i++) { h ^= fb[i]; h *= 16777619u; }
    return h;
}

static void test_machine(void)
{
    ti99 *m = ti99_create(48000);
    int16_t audio[TI99_MAX_AUDIO_FRAMES * 2];
    int n, total = 0, f;
    printf("Machine (synthetic console ROM)\n");

    build_synth_rom();
    ti99_set_console(m, srom, sgrom);
    ti99_reset(m);
    for (f = 0; f < 3; f++) { ti99_run_frame(m, audio, &n); total += n; }
    /* 3 frames of video, interrupt handler must have run once per frame */
    CHECK_EQ(ti99_peek16(m, 0x8320), 3);
    {
        const uint32_t *fb = ti99_framebuffer(m);
        /* 'A' glyph first row 0x3C: bits 2..5 set -> white on black */
        CHECK_EQ(fb[0], PAL(1)); CHECK_EQ(fb[2], PAL(15)); CHECK_EQ(fb[5], PAL(15)); CHECK_EQ(fb[6], PAL(1));
        CHECK_EQ(fb[8 + 2], PAL(15));                        /* second 'A' next to the first */
        CHECK_EQ(fb[16], PAL(4));                            /* blank cells use the (dark blue) backdrop? char 0 -> colour 0 */
    }
    CHECK(total > 2300 && total < 2500);                    /* ~800 audio frames per video frame */
    CHECK(fabs(TI99_FPS - 59.96) < 0.1);

    /* savestate round trip is deterministic */
    {
        size_t sz = ti99_state_size();
        void *st = malloc(sz);
        uint32_t h1, h2;
        CHECK(ti99_save_state(m, st, sz));
        for (f = 0; f < 5; f++) ti99_run_frame(m, audio, &n);
        h1 = fb_hash(ti99_framebuffer(m));
        {
            unsigned c1 = ti99_peek16(m, 0x8320);
            CHECK(ti99_load_state(m, st, sz));
            CHECK_EQ(ti99_peek16(m, 0x8320), 3);
            for (f = 0; f < 5; f++) ti99_run_frame(m, audio, &n);
            h2 = fb_hash(ti99_framebuffer(m));
            CHECK_EQ(h1, h2); CHECK_EQ(ti99_peek16(m, 0x8320), c1);
        }
        free(st);
    }
    ti99_destroy(m);
}

/* ---- machine: bus decode, GROM, keyboard scan via CRU, cartridge banking, 32K RAM */

static uint8_t prog_rom[0x2000];
static void PW(uint16_t w) { prog_rom[sp] = (uint8_t)(w >> 8); prog_rom[sp + 1u] = (uint8_t)w; sp = (uint16_t)(sp + 2); }
static void PLI(unsigned r, uint16_t v) { PW((uint16_t)(0x0200 | r)); PW(v); }

static void test_machine_io(void)
{
    ti99 *m = ti99_create(48000);
    int16_t audio[TI99_MAX_AUDIO_FRAMES * 2];
    int n, i;
    uint8_t keys[8];
    ti99_cart_desc cd;
    static uint8_t crom[0x6000];
    printf("Machine I/O\n");

    memset(prog_rom, 0, sizeof prog_rom);
    sp = 0; PW(0x83E0); PW(0x0020); PW(0x83C0); PW(0x0100);
    sp = 0x20;
    PW(0x02E0); PW(0x83E0);                                           /* LWPI */
    /* GROM: set address >0005, read three bytes with MOVB @>9800 */
    PLI(0, 0x0000); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9C02);           /* MOVB R0,@>9C02 (hi=0) */
    PLI(0, 0x0500); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9C02);           /* lo = 5 */
    PW(F1(0xD000, 2, 0, 0, 1)); PW(0x9800);                            /* MOVB @>9800,R1 */
    PW(F1(0xD000, 2, 0, 0, 2)); PW(0x9800);                            /* MOVB @>9800,R2 */
    /* keyboard: select column 3, read 8 rows with STCR */
    PLI(12, 0x0024); PLI(3, 0x0300); PW((uint16_t)(0x3000 | (3 << 6) | 3));       /* LDCR R3,3 */
    PLI(12, 0x0006); PW((uint16_t)(0x3400 | (8 << 6) | 4));                        /* STCR R4,8 */
    /* joystick 1 (column 6) */
    PLI(12, 0x0024); PLI(3, 0x0600); PW((uint16_t)(0x3000 | (3 << 6) | 3));
    PLI(12, 0x0006); PW((uint16_t)(0x3400 | (8 << 6) | 5));                        /* STCR R5,8 */
    /* 32K expansion: write/read >2000 and >A000 */
    PLI(6, 0x1234); PW(F1(0xC000, 0, 6, 2, 0)); PW(0x2000);
    PLI(6, 0x5678); PW(F1(0xC000, 0, 6, 2, 0)); PW(0xA000);
    PW(F1(0xC000, 2, 0, 0, 7)); PW(0x2000);                             /* MOV @>2000,R7 */
    PW(F1(0xC000, 2, 0, 0, 8)); PW(0xA000);                             /* MOV @>A000,R8 */
    /* cartridge ROM at >6000 and bank switch (type paged16k): read, switch to bank 1, read */
    PW(F1(0xC000, 2, 0, 0, 9)); PW(0x6000);                             /* MOV @>6000,R9 */
    PW(F1(0xC000, 0, 6, 2, 0)); PW(0x6002);                             /* MOV R6,@>6002 -> bank 1 */
    PW(F1(0xC000, 2, 0, 0, 10)); PW(0x6000);                            /* MOV @>6000,R10 */
    PW(F1(0xC000, 0, 6, 2, 0)); PW(0x6000);                             /* MOV R6,@>6000 -> bank 0 */
    PW(F1(0xC000, 2, 0, 0, 11)); PW(0x6000);                            /* MOV @>6000,R11 */
    PW(0x10FF);                                                         /* JMP $ */

    /* remove the placeholder store (turn into NOP-like harmless MOV R1,@>8310) is fine as is */
    for (i = 0; i < 0x6000; i++) crom[i] = (uint8_t)(i & 0xFF);
    crom[0] = 0xA1; crom[1] = 0xB2; crom[0x2000] = 0xC3; crom[0x2001] = 0xD4;
    memset(&cd, 0, sizeof cd);
    cd.type = CART_PAGED16K; cd.rom = crom; cd.rom_size = 0x4000;
    CHECK_EQ(ti99_insert_cart(m, &cd), 0);

    {
        static uint8_t gr[0x6000];
        for (i = 0; i < 0x6000; i++) gr[i] = (uint8_t)(i * 7);
        ti99_set_console(m, prog_rom, gr);
    }
    ti99_set_options(m, 1, 4);
    memset(keys, 0, sizeof keys);
    keys[3] = 1u << 4;                                  /* column 3, row 4 ('4') */
    keys[6] = (1u << 0) | (1u << 4);                    /* joystick 1: fire + up */
    ti99_set_keys(m, keys, 0);
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);

    {
        uint16_t *cpu = (uint16_t *)ti99_cpu(m);
        (void)cpu;
    }
    /* registers live in the scratchpad at >83E0 */
#define REG(k) ti99_peek16(m, (uint16_t)(0x83E0 + 2 * (k)))
    CHECK_EQ(REG(1) >> 8, (5 * 7) & 0xFF);                         /* GROM >0005 */
    CHECK_EQ(REG(2) >> 8, (6 * 7) & 0xFF);                         /* auto-increment */
    CHECK_EQ(REG(4) >> 8, (~(1u << 4)) & 0xFF);                    /* pressed key reads 0 */
    CHECK_EQ(REG(5) >> 8, (~((1u << 0) | (1u << 4))) & 0xFF);
    CHECK_EQ(REG(7), 0x1234); CHECK_EQ(REG(8), 0x5678);
    CHECK_EQ(REG(9), 0xA1B2); CHECK_EQ(REG(10), 0xC3D4); CHECK_EQ(REG(11), 0xA1B2);

    /* disable the expansion: writes vanish */
    ti99_set_options(m, 0, 4);
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);
    CHECK_EQ(REG(7), 0); CHECK_EQ(REG(8), 0);
    ti99_destroy(m);
}


/* ===================================================== loader: layouts, speech ROM */

static void test_loader_layouts(void)
{
    static uint8_t c4k[0x1000], g6k[4][0x1800], c8k[0x2000], d8k[0x2000];
    loaded_cart lc;
    char err[256];
    size_t zs, i;
    uint8_t *z;
    int k;
    printf("Loader (TI file naming, built-in MAME software list)\n");
    for (i = 0; i < 0x1000; i++) c4k[i] = (uint8_t)(i + 9);
    for (k = 0; k < 4; k++) for (i = 0; i < 0x1800; i++) g6k[k][i] = (uint8_t)(i * (k + 3) + k);
    for (i = 0; i < 0x2000; i++) { c8k[i] = (uint8_t)(i ^ 0x33); d8k[i] = (uint8_t)(i ^ 0xCC); }

    {   /* listed cartridges, as in MAME's zips (built-in list; empty system dir) */
        zfile alp[5] = { { "phm3056c.bin", c4k, 0x1000 }, { "phm3056g3.bin", g6k[0], 0x1800 }, { "phm3056g4.bin", g6k[1], 0x1800 },
                         { "phm3056g5.bin", g6k[2], 0x1800 }, { "phm3056g6.bin", g6k[3], 0x1800 } };
        z = make_zip(alp, 5, &zs);
        CHECK_EQ(loader_load_cart("alpiner.zip", z, zs, "/nonexistent", &lc, err, sizeof err), 0);
        CHECK(strstr(lc.note, "built-in") != NULL);
        CHECK_EQ(lc.desc.type, CART_STANDARD); CHECK_EQ(lc.desc.rom_size, 0x1000); CHECK_EQ(lc.desc.ngrom, 4);
        for (k = 0; k < 4; k++) {
            CHECK_EQ(lc.desc.grom[k].addr, 0x6000 + 0x2000 * k); CHECK_EQ(lc.desc.grom[k].size, 0x1800);
            CHECK_EQ(lc.desc.grom[k].data[0x123], g6k[k][0x123]);
        }
        CHECK_EQ(lc.desc.rom[0x55], c4k[0x55]);
        loaded_cart_free(&lc);
        /* the same files under an unlisted name: g3..g6 heuristic must give the same layout */
        CHECK_EQ(loader_load_cart("someremix.zip", z, zs, "/nonexistent", &lc, err, sizeof err), 0);
        CHECK(strstr(lc.note, "guessed") != NULL);
        CHECK_EQ(lc.desc.type, CART_STANDARD); CHECK_EQ(lc.desc.rom_size, 0x1000); CHECK_EQ(lc.desc.ngrom, 4);
        for (k = 0; k < 4; k++) { CHECK_EQ(lc.desc.grom[k].addr, 0x6000 + 0x2000 * k); CHECK_EQ(lc.desc.grom[k].data[7], g6k[k][7]); }
        loaded_cart_free(&lc); free(z);
    }
    {   /* GROM-only (Blasto): no ROM, GROM at >6000 */
        zfile bl[1] = { { "phm3032g3.bin", g6k[0], 0x1800 } };
        z = make_zip(bl, 1, &zs);
        CHECK_EQ(loader_load_cart("blasto.zip", z, zs, "/nonexistent", &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.rom_size, 0); CHECK_EQ(lc.desc.ngrom, 1); CHECK_EQ(lc.desc.grom[0].addr, 0x6000);
        loaded_cart_free(&lc); free(z);
    }
    {   /* Donkey Kong: two 8K ROM banks => paged16k, in c,d order */
        zfile dk[2] = { { "donkeyd.bin", d8k, 0x2000 }, { "donkeyc.bin", c8k, 0x2000 } };
        z = make_zip(dk, 2, &zs);
        CHECK_EQ(loader_load_cart("donkeykg.zip", z, zs, "/nonexistent", &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.type, CART_PAGED16K); CHECK_EQ(lc.desc.rom_size, 0x4000);
        CHECK_EQ(lc.desc.rom[3], c8k[3]); CHECK_EQ(lc.desc.rom[0x2000 + 3], d8k[3]);
        loaded_cart_free(&lc);
        CHECK_EQ(loader_load_cart("unlisted-dk.zip", z, zs, "/nonexistent", &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.type, CART_PAGED16K); CHECK_EQ(lc.desc.rom[3], c8k[3]); CHECK_EQ(lc.desc.rom[0x2000 + 3], d8k[3]);
        loaded_cart_free(&lc); free(z);
    }
    {   /* raw dump names */
        CHECK_EQ(loader_load_cart("/x/phm3057g3.bin", g6k[0], 0x1800, "", &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.ngrom, 1); CHECK_EQ(lc.desc.grom[0].addr, 0x6000); loaded_cart_free(&lc);
        CHECK_EQ(loader_load_cart("/x/phm3056g5.bin", g6k[0], 0x1800, "", &lc, err, sizeof err), 0);
        CHECK_EQ(lc.desc.grom[0].addr, 0xA000); loaded_cart_free(&lc);
    }
}

static void test_speech_loader(void)
{
    static uint8_t a[0x4000], b[0x4000], whole[0x8000], rom[0x8000];
    char err[256], note[128];
    size_t zs, i;
    uint8_t *z;
    printf("Speech ROM loader\n");
    for (i = 0; i < 0x4000; i++) { a[i] = (uint8_t)(i * 3 + 1); b[i] = (uint8_t)(i * 5 + 2); }
    memcpy(whole, a, 0x4000); memcpy(whole + 0x4000, b, 0x4000);
    {
        zfile f[2] = { { "cd2326a.u2b", b, 0x4000 }, { "cd2325a.u2a", a, 0x4000 } };   /* order in the zip must not matter */
        z = make_zip(f, 2, &zs);
        CHECK_EQ(loader_parse_speech_zip(z, zs, rom, note, sizeof note, err, sizeof err), 0);
        CHECK(memcmp(rom, whole, 0x8000) == 0);
        CHECK(strstr(note, "CRC differs") != NULL);          /* synthetic data is not MAME's dump */
        free(z);
    }
    {
        zfile f[1] = { { "speech32k.bin", whole, 0x8000 } };
        z = make_zip(f, 1, &zs);
        CHECK_EQ(loader_parse_speech_zip(z, zs, rom, note, sizeof note, err, sizeof err), 0);
        CHECK(memcmp(rom, whole, 0x8000) == 0);
        free(z);
    }
    {
        zfile f[1] = { { "readme.txt", a, 100 } };
        z = make_zip(f, 1, &zs);
        CHECK(loader_parse_speech_zip(z, zs, rom, note, sizeof note, err, sizeof err) != 0);
        CHECK(strstr(err, "cd2325a") != NULL);
        free(z);
    }
    {   /* search the system directory, then fall back to "not found" (rc 1, not an error) */
        zfile f[2] = { { "cd2325a.u2a", a, 0x4000 }, { "cd2326a.u2b", b, 0x4000 } };
        int rc;
        z = make_zip(f, 2, &zs);
        if (system("mkdir -p /tmp/ti99_test_sys/mame")) return;
        write_file("/tmp/ti99_test_sys/mame/ti99_speech.zip", z, zs);
        memset(rom, 0, sizeof rom);
        rc = loader_load_speech("/tmp/ti99_test_sys", NULL, rom, note, sizeof note, err, sizeof err);
        CHECK_EQ(rc, 0); CHECK(memcmp(rom, whole, 0x8000) == 0);
        remove("/tmp/ti99_test_sys/mame/ti99_speech.zip");
        rc = loader_load_speech("/tmp/ti99_test_sys_none", NULL, rom, note, sizeof note, err, sizeof err);
        CHECK_EQ(rc, 1);
        free(z);
    }
}

/* ======================================== speech synthesizer (chip + TI bus) */

static uint8_t bitrev8(uint8_t v)
{
    uint8_t r = 0; int i;
    for (i = 0; i < 8; i++) if (v & (1u << i)) r |= (uint8_t)(0x80u >> i);
    return r;
}

static void test_speech_chip(void)
{
    static uint8_t rom[SPEECH_ROM_SIZE];
    speech s;
    uint8_t v;
    int i, stall;
    printf("Speech synthesizer (chip)\n");
    for (i = 0; i < (int)SPEECH_ROM_SIZE; i++) rom[i] = (uint8_t)(i * 7 + (i >> 8));
    speech_init(&s, rom);
    speech_reset(&s);

    stall = speech_read(&s, &v);
    CHECK_EQ(v & 0xE0, 0x60);                               /* idle: buffer low + buffer empty, not talking */
    CHECK_EQ(stall, 75);
    CHECK(!speech_talking(&s));

    /* LOAD ADDRESS: five nibbles, then READ BYTE returns ROM bytes with auto-increment */
    speech_write(&s, 0x40 | 0x3); speech_write(&s, 0x40 | 0x2); speech_write(&s, 0x40 | 0x1);
    speech_write(&s, 0x40 | 0x0); speech_write(&s, 0x40 | 0x0);
    {
        /* MAME serves the VSM bits LSB-first and READ BYTE shifts them in MSB-first, so the byte read
         * is the bit-reversed ROM byte.  LOAD ADDRESS takes the low nibble first: 3,2,1,0,0 -> >0123. */
        uint8_t a, b2;
        speech_write(&s, 0x10); speech_read(&s, &a);
        speech_write(&s, 0x10); speech_read(&s, &b2);
        CHECK_EQ(a, bitrev8(rom[0x0123])); CHECK_EQ(b2, bitrev8(rom[0x0124]));
    }

    /* SPEAK EXTERNAL: speech only starts once the FIFO is more than half full (the 9th byte);
     * FIFO status follows the byte count; stop frames end speech */
    speech_write(&s, 0x70); speech_reset(&s);
    speech_write(&s, 0x60);
    speech_read(&s, &v);
    CHECK_EQ(v & 0x80, 0);                                  /* not talking yet, buffer empty */
    CHECK(v & 0x20);
    for (i = 0; i < 8; i++) speech_write(&s, 0x00);
    speech_read(&s, &v);
    CHECK_EQ(v & 0x80, 0); CHECK(v & 0x40);                 /* 8 bytes: still "buffer low", still waiting */
    for (i = 0; i < 4; i++) speech_write(&s, 0x00);         /* 12 bytes of zeros: energy 0 silence frames */
    CHECK(speech_talking(&s));
    speech_read(&s, &v);
    CHECK(v & 0x80);                                        /* TS */
    CHECK_EQ(v & 0x20, 0);                                  /* buffer not empty */
    CHECK_EQ(v & 0x40, 0);                                  /* more than 8 bytes: buffer low is clear */
    for (i = 0; i < 60; i++) speech_write(&s, 0xFF);        /* stop frames */
    for (i = 0; i < 40000; i++) speech_advance(&s, 375);
    speech_read(&s, &v);
    CHECK_EQ(v & 0x80, 0);                                  /* talking has stopped */
    CHECK_EQ(v & 0x60, 0x60);                               /* ... and the buffer drained */

    /* writes to a full FIFO stall the CPU instead of losing data */
    speech_reset(&s);
    speech_write(&s, 0x60);
    {
        int total = 0;
        for (i = 0; i < 40; i++) total += speech_write(&s, 0x00);
        CHECK(total > 40 * 75);
    }
    /* output stays bounded */
    {
        int mx = 0, o;
        speech_reset(&s); speech_write(&s, 0x60);
        for (i = 0; i < 64; i++) speech_write(&s, (uint8_t)(i * 37 + 11));
        for (i = 0; i < 20000; i++) { speech_advance(&s, 375); o = speech_output(&s); if (o < 0) o = -o; if (o > mx) mx = o; }
        CHECK(mx <= 32768);
    }
}

static uint32_t audio_hash(const int16_t *a, int frames, uint32_t h)
{
    int i;
    for (i = 0; i < frames * 2; i++) { h ^= (uint16_t)a[i]; h *= 16777619u; }
    return h;
}

static void test_speech_machine(void)
{
    static uint8_t srom_[SPEECH_ROM_SIZE], gr[0x6000];
    static int16_t audio[TI99_MAX_AUDIO_FRAMES * 2];
    ti99 *m = ti99_create(48000);
    int n, f, i, nonzero_diff = 0;
    printf("Speech synthesizer (TI bus, savestate)\n");
    for (i = 0; i < (int)SPEECH_ROM_SIZE; i++) srom_[i] = (uint8_t)(i * 13 + 5);
    for (i = 0; i < 0x6000; i++) gr[i] = (uint8_t)i;

    /* program: reset chip, read status, speak-external, write bytes forever */
    memset(prog_rom, 0, sizeof prog_rom);
    sp = 0; PW(0x83E0); PW(0x0020); PW(0x83C0); PW(0x0100);
    sp = 0x20;
    PW(0x02E0); PW(0x83E0);                                           /* LWPI >83E0 */
    PLI(0, 0x7000); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400);            /* MOVB R0,@>9400 : RESET */
    PW(F1(0xD000, 2, 0, 0, 1)); PW(0x9000);                            /* MOVB @>9000,R1 : status */
    PLI(0, 0x6000); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400);            /* SPEAK EXTERNAL */
    PW(F1(0xD000, 2, 0, 0, 2)); PW(0x9000);                            /* status -> R2 */
    PLI(0, 0x1357);
    PW(0x0220); PW(0x0B35);                                            /* loop: AI R0,>0B35 */
    PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400);                            /*       MOVB R0,@>9400 */
    PW(0x10FB);                                                        /*       JMP loop */

    ti99_set_console(m, prog_rom, gr);
    CHECK(!ti99_speech_active(m));                          /* no ROM yet: port is not mapped */
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);
    CHECK_EQ(ti99_peek16(m, 0x83E2) >> 8, 0);               /* status reads 0 on an unmapped port */

    ti99_set_speech_rom(m, srom_);
    CHECK(ti99_speech_active(m));
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);
#define SREG(k) ti99_peek16(m, (uint16_t)(0x83E0 + 2 * (k)))
    CHECK_EQ(SREG(1) >> 8 & 0xE0, 0x60);                    /* after reset: buffer low + empty */
    CHECK_EQ(SREG(2) >> 8 & 0x80, 0);                       /* SPEAK EXTERNAL alone does not start speech */

    /* speech must be audible, and removable by the core option */
    {
        uint32_t on = 2166136261u, off = 2166136261u;
        for (f = 0; f < 20; f++) { ti99_run_frame(m, audio, &n); on = audio_hash(audio, n, on); }
        ti99_set_speech_enabled(m, 0);
        CHECK(!ti99_speech_active(m));
        ti99_reset(m);
        for (f = 0; f < 21; f++) { ti99_run_frame(m, audio, &n); if (f) off = audio_hash(audio, n, off); }
        nonzero_diff = (on != off);
        CHECK(nonzero_diff);
        ti99_set_speech_enabled(m, 1);
        CHECK(ti99_speech_active(m));
    }

    /* savestate round trip while talking: identical audio afterwards */
    {
        size_t sz = ti99_state_size();
        void *st = malloc(sz);
        uint32_t h1 = 2166136261u, h2 = 2166136261u;
        ti99_reset(m);
        for (f = 0; f < 6; f++) ti99_run_frame(m, audio, &n);
        CHECK(ti99_speech_talking(m));
        CHECK(ti99_save_state(m, st, sz));
        for (f = 0; f < 12; f++) { ti99_run_frame(m, audio, &n); h1 = audio_hash(audio, n, h1); }
        CHECK(ti99_load_state(m, st, sz));
        for (f = 0; f < 12; f++) { ti99_run_frame(m, audio, &n); h2 = audio_hash(audio, n, h2); }
        CHECK_EQ(h1, h2);
        CHECK(!ti99_load_state(m, st, sz - 1));              /* truncated state is rejected */

        /* hostile savestate: every speech-state byte randomised must not crash or run away */
        {
            uint8_t *bad = malloc(sz);
            unsigned seed = 12345u, trial;
            for (trial = 0; trial < 200; trial++) {
                size_t k;
                memcpy(bad, st, sz);
                for (k = sz - SPEECH_SAVED_SIZE; k < sz; k++) { seed = seed * 1103515245u + 12345u; bad[k] = (uint8_t)(seed >> 16); }
                CHECK(ti99_load_state(m, bad, sz));
                for (f = 0; f < 2; f++) ti99_run_frame(m, audio, &n);
            }
            free(bad);
        }
        free(st);
    }
    ti99_destroy(m);
}


/* speech timing and lifecycle on the TI bus */
static void test_speech_timing(void)
{
    static uint8_t srom_[SPEECH_ROM_SIZE], gr[0x6000];
    static int16_t audio[TI99_MAX_AUDIO_FRAMES * 2];
    ti99 *m = ti99_create(48000);
    int n, f, i;
    unsigned iters_on, iters_off;
    printf("Speech synthesizer (timing, reset, enable flag)\n");
    for (i = 0; i < (int)SPEECH_ROM_SIZE; i++) srom_[i] = (uint8_t)(i * 3);
    for (i = 0; i < 0x6000; i++) gr[i] = (uint8_t)i;

    /* A: speak external with 12 stop-frame bytes, then idle: only elapsed CPU time can finish the speech */
    memset(prog_rom, 0, sizeof prog_rom);
    sp = 0; PW(0x83E0); PW(0x0020); PW(0x83C0); PW(0x0100);
    sp = 0x20;
    PW(0x02E0); PW(0x83E0);
    PLI(0, 0x7000); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400);
    PLI(0, 0x6000); PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400);
    PLI(0, 0xFF00);
    for (i = 0; i < 12; i++) { PW(F1(0xD000, 0, 0, 2, 0)); PW(0x9400); }
    PW(0x10FF);                                                         /* JMP $ */
    ti99_set_console(m, prog_rom, gr);
    ti99_set_speech_rom(m, srom_);
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);
    CHECK(ti99_speech_talking(m));                                      /* started, stop frame not consumed yet */
    for (f = 0; f < 6 && ti99_speech_talking(m); f++) ti99_run_frame(m, audio, &n);
    CHECK(!ti99_speech_talking(m));                                     /* the chip ran in CPU time and hit the stop frame */
    CHECK(f < 6);

    /* reset puts the synthesizer back to idle immediately */
    ti99_reset(m);
    ti99_run_frame(m, audio, &n);
    CHECK(ti99_speech_talking(m));
    ti99_reset(m);
    CHECK(!ti99_speech_talking(m));

    /* the enable flag survives (re)loading a ROM */
    ti99_set_speech_enabled(m, 0);
    ti99_set_speech_rom(m, srom_);
    CHECK(!ti99_speech_active(m));
    ti99_set_speech_enabled(m, 1);
    CHECK(ti99_speech_active(m));
    ti99_set_speech_rom(m, NULL);
    CHECK(!ti99_speech_active(m));
    ti99_set_speech_enabled(m, 1);
    CHECK(!ti99_speech_active(m));                                      /* nothing to enable without a ROM */
    ti99_set_speech_rom(m, srom_);
    CHECK(ti99_speech_active(m));

    /* B: a tight loop of status reads is slowed by the chip's access time (about 75 clocks per read) */
    memset(prog_rom, 0, sizeof prog_rom);
    sp = 0; PW(0x83E0); PW(0x0020); PW(0x83C0); PW(0x0100);
    sp = 0x20;
    PW(0x02E0); PW(0x83E0);
    PW(F1(0xD000, 2, 0, 0, 1)); PW(0x9000);                             /* loop: MOVB @>9000,R1 */
    PW(0x0587);                                                         /*       INC R7 */
    PW(0x10FC);                                                         /*       JMP loop */
    ti99_set_console(m, prog_rom, gr);
    ti99_reset(m);
    for (f = 0; f < 4; f++) ti99_run_frame(m, audio, &n);
    iters_on = ti99_peek16(m, 0x83EE);
    ti99_set_speech_enabled(m, 0);
    ti99_reset(m);
    for (f = 0; f < 4; f++) ti99_run_frame(m, audio, &n);
    iters_off = ti99_peek16(m, 0x83EE);
    CHECK(iters_on > 0 && iters_off > 0);
    {   /* 4 frames = 4 * 262 lines * 191 clocks; the difference per loop iteration is the chip's access time */
        double c_on = 4.0 * 262 * 191 / iters_on, c_off = 4.0 * 262 * 191 / iters_off;
        CHECK(c_on - c_off > 70.0 && c_on - c_off < 80.0);
    }
    ti99_destroy(m);
}

/* ============================================== on-screen keyboard logic */

static int osk_find(const char *label, int *row, int *col)
{
    int r, c;
    for (r = 0; r < osk_row_count(); r++)
        for (c = 0; c < osk_col_count(r); c++)
            if (!strcmp(osk_label(r, c), label)) { *row = r; *col = c; return 1; }
    return 0;
}

static int osk_has(const uint8_t k[8], uint8_t key) { return (k[key >> 3] >> (key & 7)) & 1; }

static void osk_goto(osk *o, const char *label)
{
    int r = 0, c = 0;
    uint8_t keys[8];
    int at;
    CHECK(osk_find(label, &r, &c));
    o->row = (uint8_t)r; o->col = (uint8_t)c;
    memset(keys, 0, 8); osk_frame(o, 0, keys, &at);
}

static void test_osk(void)
{
    osk o;
    uint8_t keys[8];
    int at, f, held_frames;
    static uint32_t fb[OSK_W * OSK_H_PX];
    printf("On-screen keyboard\n");
    osk_init(&o);
    CHECK(!osk_is_open(&o));

    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);        /* closed: ignores presses */
    CHECK(!osk_has(keys, K_A) && !osk_has(keys, K_1));
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    memset(keys, 0, 8); osk_frame(&o, OSK_TOGGLE, keys, &at);
    CHECK(osk_is_open(&o));
    memset(keys, 0, 8); osk_frame(&o, OSK_TOGGLE, keys, &at);       /* held toggle must not flicker */
    CHECK(osk_is_open(&o));
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);

    /* a quick tap holds the key long enough for the console to scan it */
    osk_goto(&o, "A");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    CHECK(osk_has(keys, K_A));
    held_frames = 1;
    for (f = 0; f < 10; f++) { memset(keys, 0, 8); osk_frame(&o, 0, keys, &at); if (osk_has(keys, K_A)) held_frames++; }
    CHECK(held_frames >= 3 && held_frames <= 6);

    /* sticky SHIFT: one-shot applies to a single key; pressed twice it locks; third press frees it */
    osk_goto(&o, "SHIFT");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    CHECK(osk_has(keys, K_SHIFT));
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    osk_goto(&o, "1");
    for (f = 0; f < 3; f++) { memset(keys, 0, 8); osk_frame(&o, f == 0 ? OSK_PRESS : OSK_PRESS, keys, &at); }
    CHECK(osk_has(keys, K_1) && osk_has(keys, K_SHIFT));
    for (f = 0; f < 8; f++) { memset(keys, 0, 8); osk_frame(&o, 0, keys, &at); }
    CHECK(!osk_has(keys, K_SHIFT));                                  /* one-shot used up */
    osk_goto(&o, "SHIFT");
    for (f = 0; f < 2; f++) { memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at); memset(keys, 0, 8); osk_frame(&o, 0, keys, &at); }
    osk_goto(&o, "2");
    for (f = 0; f < 8; f++) { memset(keys, 0, 8); osk_frame(&o, f < 3 ? OSK_PRESS : 0, keys, &at); }
    CHECK(osk_has(keys, K_SHIFT));                                   /* locked: still held after a key */
    osk_goto(&o, "SHIFT");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    CHECK(!osk_has(keys, K_SHIFT));

    /* FCTN shortcuts and CAPS */
    osk_goto(&o, "QUIT");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    CHECK(osk_has(keys, K_EQ) && osk_has(keys, K_FCTN));
    for (f = 0; f < 8; f++) { memset(keys, 0, 8); osk_frame(&o, 0, keys, &at); }
    osk_goto(&o, "CAPS");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    CHECK_EQ(at, 1);
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);         /* still held: no second toggle */
    CHECK_EQ(at, 0);
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);

    /* navigation wraps and vertical moves keep the column */
    o.row = 0; o.col = 0; memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    memset(keys, 0, 8); osk_frame(&o, OSK_LEFT, keys, &at);
    CHECK_EQ(o.col, osk_col_count(0) - 1);
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    memset(keys, 0, 8); osk_frame(&o, OSK_UP, keys, &at);
    CHECK_EQ(o.row, osk_row_count() - 1);
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    {
        int r, c, n = 0;
        for (r = 0; r < osk_row_count(); r++) for (c = 0; c < osk_col_count(r); c++) n++;
        CHECK(n >= 50);
    }
    /* every printable key of the console is present exactly once */
    {
        static const char *all[] = { "A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
                                     "0","1","2","3","4","5","6","7","8","9","=","/",";",",",".","ENTER","SPACE","SHIFT","CTRL","FCTN" };
        unsigned i;
        for (i = 0; i < sizeof all / sizeof all[0]; i++) {
            int r, c, cnt = 0;
            for (r = 0; r < osk_row_count(); r++) for (c = 0; c < osk_col_count(r); c++) if (!strcmp(osk_label(r, c), all[i])) cnt++;
            CHECK_EQ(cnt, 1);
        }
    }
    /* closing releases everything (no stuck keys) */
    osk_goto(&o, "CTRL");
    memset(keys, 0, 8); osk_frame(&o, OSK_PRESS, keys, &at);
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    CHECK(osk_has(keys, K_CTRL));
    memset(keys, 0, 8); osk_frame(&o, OSK_CLOSE, keys, &at);
    CHECK(!osk_is_open(&o)); CHECK(!osk_has(keys, K_CTRL));
    memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
    CHECK(!osk_has(keys, K_CTRL));

    /* drawing: both placements, never touches pixels outside the panel at the other edge */
    {
        int i, changed_top = 0, changed_bottom = 0;
        memset(keys, 0, 8); osk_frame(&o, OSK_TOGGLE, keys, &at);
        memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
        for (i = 0; i < OSK_W * OSK_H_PX; i++) fb[i] = 0xFF123456u;
        osk_draw(&o, fb, 0);
        for (i = 0; i < OSK_W * 20; i++) if (fb[i] != 0xFF123456u) changed_top = 1;
        for (i = OSK_W * (OSK_H_PX - 20); i < OSK_W * OSK_H_PX; i++) if (fb[i] != 0xFF123456u) changed_bottom = 1;
        CHECK(!changed_top && changed_bottom);
        memset(keys, 0, 8); osk_frame(&o, OSK_MOVE, keys, &at);
        memset(keys, 0, 8); osk_frame(&o, 0, keys, &at);
        for (i = 0; i < OSK_W * OSK_H_PX; i++) fb[i] = 0xFF123456u;
        osk_draw(&o, fb, 1);
        changed_top = changed_bottom = 0;
        for (i = 0; i < OSK_W * 20; i++) if (fb[i] != 0xFF123456u) changed_top = 1;
        for (i = OSK_W * (OSK_H_PX - 20); i < OSK_W * OSK_H_PX; i++) if (fb[i] != 0xFF123456u) changed_bottom = 1;
        CHECK(changed_top && !changed_bottom);
    }
}

int main(int argc, char **argv)
{
    test_cpu();
    test_vdp();
    test_psg();
    test_zip(argc > 1 ? argv[1] : NULL);
    test_loader();
    test_machine();
    test_machine_io();
    test_loader_layouts();
    test_speech_loader();
    test_speech_chip();
    test_speech_machine();
    test_speech_timing();
    test_osk();
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
