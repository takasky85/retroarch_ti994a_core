/* Minimal libretro "frontend" for testing the built core as a shared library (Linux/macOS: dlopen).
 *   usage: host_test ./ti99_libretro.so
 * It fabricates a system directory with a synthetic ti99_4a.zip (tiny test program, NOT a real
 * TI BIOS), loads a cartridge, injects keyboard / joypad input and checks what the core does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <time.h>

#include "libretro.h"
#include "zipread.h"

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- core entry points ---- */
#define FN(ret, name, args) static ret (*name) args
FN(void, c_set_env, (retro_environment_t));
FN(void, c_set_video, (retro_video_refresh_t));
FN(void, c_set_audio, (retro_audio_sample_t));
FN(void, c_set_audio_batch, (retro_audio_sample_batch_t));
FN(void, c_set_poll, (retro_input_poll_t));
FN(void, c_set_input, (retro_input_state_t));
FN(void, c_init, (void));
FN(void, c_deinit, (void));
FN(void, c_get_info, (struct retro_system_info *));
FN(void, c_get_av, (struct retro_system_av_info *));
FN(bool, c_load, (const struct retro_game_info *));
FN(void, c_unload, (void));
FN(void, c_run, (void));
FN(void, c_reset, (void));
FN(size_t, c_ser_size, (void));
FN(bool, c_ser, (void *, size_t));
FN(bool, c_unser, (const void *, size_t));
FN(void *, c_mem_data, (unsigned));
FN(size_t, c_mem_size, (unsigned));

/* ---- fake frontend state ---- */
static char sysdir[256] = "/tmp/ti99_host_sys";
static retro_keyboard_event_t kbd_cb;
static unsigned frames_seen, last_w, last_h;
static size_t audio_frames;
static uint32_t first_pixels[16], bottom_row[256], top_row[256];
static const char *option_speech = "enabled";
static uint8_t pad_state[2][16];          /* [port][id] */
static const char *option_expansion = "enabled";
static int g_need_fullpath;

/* Behave like RetroArch: with need_fullpath the core gets only a path to a real file, otherwise a data buffer.
 * The content is written to a temp file first so the path is real. */
static void wfile_(const char *p, const void *d, size_t n) { FILE *f = fopen(p, "wb"); if (f) { fwrite(d, 1, n, f); fclose(f); } }
static bool load_content(const char *name, const uint8_t *data, size_t n, int force_data)
{
    struct retro_game_info g;
    char path[300];
    memset(&g, 0, sizeof g);
    snprintf(path, sizeof path, "/tmp/ti99_host_content/%s", name);
    if (system("mkdir -p /tmp/ti99_host_content")) return false;
    wfile_(path, data, n);
    g.path = path;
    if (!g_need_fullpath || force_data) { g.data = data; g.size = n; }
    return c_load(&g);
}

static bool env(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: kbd_cb = ((struct retro_keyboard_callback *)data)->callback; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "ti99_expansion")) { v->value = option_expansion; return true; }
        if (!strcmp(v->key, "ti99_speech")) { v->value = option_speech; return true; }
        return false; }
    case RETRO_ENVIRONMENT_SET_MESSAGE: printf("  [core message] %s\n", ((struct retro_message *)data)->msg); return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    default: return false;
    }
}
static void video(const void *d, unsigned w, unsigned h, size_t pitch)
{
    frames_seen++; last_w = w; last_h = h;
    if (d && pitch == w * 4u) {
        memcpy(first_pixels, d, sizeof first_pixels);
        if (w == 256 && h == 192) { memcpy(top_row, d, sizeof top_row); memcpy(bottom_row, (const uint32_t *)d + 191 * 256, sizeof bottom_row); }
    }
}
static void audio_s(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_b(const int16_t *d, size_t n) { (void)d; audio_frames += n; return n; }
static void poll(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
    (void)idx;
    if (dev == RETRO_DEVICE_JOYPAD && port < 2 && id < 16) return pad_state[port][id];
    return 0;
}

/* ---- synthetic BIOS ---- */
static uint8_t rom[0x2000], grom[0x6000];
static uint16_t sp;
static void W(uint16_t w) { rom[sp] = (uint8_t)(w >> 8); rom[sp + 1u] = (uint8_t)w; sp = (uint16_t)(sp + 2); }
static void LI(unsigned r, uint16_t v) { W((uint16_t)(0x0200 | r)); W(v); }

static void scan_column(unsigned col, uint16_t store_at)
{
    LI(12, 0x0024); LI(3, (uint16_t)(col << 8)); W((uint16_t)(0x3000 | (3 << 6) | 3));   /* LDCR R3,3 */
    LI(12, 0x0006); W((uint16_t)(0x3400 | (8 << 6) | 4));                                /* STCR R4,8 */
    W((uint16_t)(0xC000 | (2 << 10) | (4 << 0))); W(store_at);                           /* MOV R4,@store */
}

static void build_bios(void)
{
    memset(rom, 0, sizeof rom); memset(grom, 0, sizeof grom);
    sp = 0; W(0x83E0); W(0x0020); W(0x83C0); W(0x0100);
    sp = 0x20;
    W(0x02E0); W(0x83E0);                       /* LWPI */
    /* main loop: scan columns 0,5 (shift, A/slash) + joystick columns 6,7, store to expansion RAM */
    {
        uint16_t loop = sp;
        scan_column(0, 0x2000); scan_column(5, 0x2002); scan_column(6, 0x2004); scan_column(7, 0x2006);
        scan_column(1, 0x2008);
        W((uint16_t)(0x1000 | (((int)loop - ((int)sp + 2)) / 2 & 0xFF)));               /* JMP loop */
    }
}

/* tiny stored-zip writer */
static uint8_t *mkzip(const char **names, const uint8_t **data, const uint32_t *sizes, int n, size_t *outsz)
{
    size_t cap = 1 << 16, len = 0, cd;
    uint8_t *b = malloc(cap);
    uint32_t off[8];
    int i;
#define P8(v)  b[len++] = (uint8_t)(v)
#define P16(v) do { P8((v) & 255); P8(((v) >> 8) & 255); } while (0)
#define P32(v) do { P16((v) & 0xFFFF); P16(((v) >> 16) & 0xFFFF); } while (0)
    for (i = 0; i < n; i++) {
        uint32_t crc = crc32_calc(data[i], sizes[i]); size_t k;
        off[i] = (uint32_t)len;
        P32(0x04034B50u); P16(20); P16(0); P16(0); P16(0); P16(0); P32(crc); P32(sizes[i]); P32(sizes[i]);
        P16(strlen(names[i])); P16(0);
        for (k = 0; names[i][k]; k++) P8(names[i][k]);
        memcpy(b + len, data[i], sizes[i]); len += sizes[i];
    }
    cd = len;
    for (i = 0; i < n; i++) {
        uint32_t crc = crc32_calc(data[i], sizes[i]); size_t k;
        P32(0x02014B50u); P16(20); P16(20); P16(0); P16(0); P16(0); P16(0); P32(crc); P32(sizes[i]); P32(sizes[i]);
        P16(strlen(names[i])); P16(0); P16(0); P16(0); P16(0); P32(0); P32(off[i]);
        for (k = 0; names[i][k]; k++) P8(names[i][k]);
    }
    {
        size_t cds = len - cd;
        P32(0x06054B50u); P16(0); P16(0); P16(n); P16(n); P32((uint32_t)cds); P32((uint32_t)cd); P16(0);
    }
    *outsz = len;
    return b;
}
static void wfile(const char *p, const void *d, size_t n) { FILE *f = fopen(p, "wb"); if (f) { fwrite(d, 1, n, f); fclose(f); } }

static uint16_t xram(void *m, unsigned off) { uint8_t *p = m; return (uint16_t)((p[off] << 8) | p[off + 1u]); }
#define COLBITS(off) (xram(mem, (off)) >> 8)
static void key(int down, unsigned code) { kbd_cb(down != 0, code, 0, 0); }
static void frames(int n) { while (n--) c_run(); }

int main(int argc, char **argv)
{
    void *h;
    struct retro_system_info info;
    struct retro_system_av_info av;
    struct retro_game_info game;
    uint8_t *mem;
    char cmd[600], path[300];

    if (argc < 2) { fprintf(stderr, "usage: %s core.so\n", argv[0]); return 2; }
    h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
#define LOAD(var, name) do { *(void **)&var = dlsym(h, name); if (!var) { fprintf(stderr, "missing %s\n", name); return 2; } } while (0)
    LOAD(c_set_env, "retro_set_environment"); LOAD(c_set_video, "retro_set_video_refresh");
    LOAD(c_set_audio, "retro_set_audio_sample"); LOAD(c_set_audio_batch, "retro_set_audio_sample_batch");
    LOAD(c_set_poll, "retro_set_input_poll"); LOAD(c_set_input, "retro_set_input_state");
    LOAD(c_init, "retro_init"); LOAD(c_deinit, "retro_deinit"); LOAD(c_get_info, "retro_get_system_info");
    LOAD(c_get_av, "retro_get_system_av_info"); LOAD(c_load, "retro_load_game"); LOAD(c_unload, "retro_unload_game");
    LOAD(c_run, "retro_run"); LOAD(c_reset, "retro_reset"); LOAD(c_ser_size, "retro_serialize_size");
    LOAD(c_ser, "retro_serialize"); LOAD(c_unser, "retro_unserialize"); LOAD(c_mem_data, "retro_get_memory_data");
    LOAD(c_mem_size, "retro_get_memory_size");

    /* system dir with the synthetic console */
    build_bios();
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", sysdir, sysdir); if (system(cmd)) return 2;
    {
        static uint8_t hi[0x1000], lo[0x1000];
        const char *nm[5] = { "994a_grom0.u500", "994a_grom1.u501", "994a_grom2.u502", "994a_rom_hb.u610", "994a_rom_lb.u611" };
        const uint8_t *dt[5]; uint32_t sizes[5] = { 0x1800, 0x1800, 0x1800, 0x1000, 0x1000 };
        size_t zs, i; uint8_t *z;
        for (i = 0; i < 0x1000; i++) { hi[i] = rom[2 * i]; lo[i] = rom[2 * i + 1]; }
        dt[0] = grom; dt[1] = grom + 0x2000; dt[2] = grom + 0x4000; dt[3] = hi; dt[4] = lo;
        z = mkzip(nm, dt, sizes, 5, &zs);
        snprintf(path, sizeof path, "%s/ti99_4a.zip", sysdir); wfile(path, z, zs); free(z);
    }

    c_set_env(env); c_set_video(video); c_set_audio(audio_s); c_set_audio_batch(audio_b);
    c_set_poll(poll); c_set_input(input);

    printf("Host\n");
    c_get_info(&info);
    CHECK(!strcmp(info.library_name, "TI-99/4A")); CHECK(info.need_fullpath); CHECK(info.block_extract);        /* path only, never extracted by the frontend */
    g_need_fullpath = info.need_fullpath;
    c_init();

    /* ---- no cartridge: boot into the synthetic ROM ---- */
    memset(&game, 0, sizeof game);
    CHECK(c_load(NULL));
    c_get_av(&av);
    CHECK(av.geometry.base_width == 256 && av.geometry.base_height == 192);
    CHECK(av.timing.fps > 59.9 && av.timing.fps < 60.0); CHECK(av.timing.sample_rate == 48000.0);
    CHECK(kbd_cb != NULL);

    if (argc > 2 && !strcmp(argv[2], "bench") && argc == 3) {          /* speed check: busy keyboard-scan loop */
        clock_t t0 = clock(); double s;
        frames(3000);
        s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("bench: 3000 frames (50 s of emulated time) in %.3f s -> %.0fx real time\n", s, 50.0 / (s > 0 ? s : 1e-9));
        return 0;
    }
    frames(3);
    CHECK(frames_seen == 3 && last_w == 256 && last_h == 192);
    CHECK(audio_frames > 2300 && audio_frames < 2500);
    mem = c_mem_data(RETRO_MEMORY_SYSTEM_RAM);
    CHECK(mem != NULL && c_mem_size(RETRO_MEMORY_SYSTEM_RAM) == 0x8000);
    if (!mem) return 1;

    /* idle keyboard reads all ones */
    CHECK(COLBITS(0) == 0xFF && COLBITS(2) == 0xFF && COLBITS(4) == 0xFF && COLBITS(6) == 0xFF && COLBITS(8) == 0xFF);

    /* plain key: 'A' is column 5 row 5; no shift/fctn */
    key(1, RETROK_a); frames(2);
    CHECK(COLBITS(2) == (0xFF & ~(1u << 5)));
    CHECK(COLBITS(0) == 0xFF);
    key(0, RETROK_a); frames(2);
    CHECK(COLBITS(2) == 0xFF);

    /* shift passes through: SHIFT is column 0 row 5 */
    key(1, RETROK_LSHIFT); key(1, RETROK_a); frames(2);
    CHECK(COLBITS(0) == (0xFF & ~(1u << 5))); CHECK(COLBITS(2) == (0xFF & ~(1u << 5)));
    key(0, RETROK_a); key(0, RETROK_LSHIFT); frames(2);

    /* Alt = FCTN (column 0 row 4) */
    key(1, RETROK_LALT); frames(2); CHECK(COLBITS(0) == (0xFF & ~(1u << 4))); key(0, RETROK_LALT); frames(2);

    /* PC '-' (no shift) -> TI SHIFT + '/'; PC ESC -> FCTN+9 (BACK) */
    key(1, RETROK_MINUS); frames(2);
    CHECK(COLBITS(0) == (0xFF & ~(1u << 5)));                      /* SHIFT held */
    CHECK(COLBITS(2) == (0xFF & ~(1u << 0)));                      /* '/' is col 5 row 0 */
    key(0, RETROK_MINUS); frames(2);
    key(1, RETROK_LEFT); frames(2);                                /* LEFT = FCTN+S */
    CHECK(COLBITS(0) == (0xFF & ~(1u << 4)));                      /* FCTN held, no SHIFT */
    CHECK(COLBITS(8) == (0xFF & ~(1u << 5)));                      /* 'S' = col 1 row 5 */
    key(0, RETROK_LEFT); frames(2);

    /* joystick 1: RetroPad -> column 6 (fire bit0, left 1, right 2, down 3, up 4) */
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 1; pad_state[0][RETRO_DEVICE_ID_JOYPAD_UP] = 1; frames(2);
    CHECK(COLBITS(4) == (0xFF & ~((1u << 0) | (1u << 4))));
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 0; pad_state[0][RETRO_DEVICE_ID_JOYPAD_UP] = 0;
    pad_state[1][RETRO_DEVICE_ID_JOYPAD_LEFT] = 1; frames(2);
    CHECK(COLBITS(6) == (0xFF & ~(1u << 1)));                      /* joystick 2 on column 7 */
    pad_state[1][RETRO_DEVICE_ID_JOYPAD_LEFT] = 0; frames(2);
    CHECK(COLBITS(4) == 0xFF && COLBITS(6) == 0xFF);

    /* Start -> ENTER (col 0 row 2) */
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_START] = 1; frames(2); CHECK(COLBITS(0) == (0xFF & ~(1u << 2)));
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_START] = 0; frames(2);

    /* Y -> SPACE (col 0 row 1); SELECT no longer types SPACE, it opens the on-screen keyboard */
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_Y] = 1; frames(2); CHECK(COLBITS(0) == (0xFF & ~(1u << 1)));
    pad_state[0][RETRO_DEVICE_ID_JOYPAD_Y] = 0; frames(2); CHECK(COLBITS(0) == 0xFF);

    {   /* ---- on-screen keyboard ---- */
        uint32_t before[256];
        int i, diff_bottom = 0;
        memcpy(before, bottom_row, sizeof before);
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = 1; frames(2);
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = 0; frames(2);
        for (i = 0; i < 256; i++) if (bottom_row[i] != before[i]) diff_bottom = 1;
        CHECK(diff_bottom);                                         /* keyboard panel is drawn over the picture */
        CHECK(COLBITS(0) == 0xFF && COLBITS(2) == 0xFF);            /* opening it types nothing */

        /* while open: B presses the highlighted key (starts on 'A') instead of firing the joystick */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 1; frames(3);
        CHECK(COLBITS(2) == (0xFF & ~(1u << 5)));                   /* 'A' */
        CHECK(COLBITS(4) == 0xFF);                                  /* joystick 1 fire is blocked */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 0; frames(8);
        CHECK(COLBITS(2) == 0xFF);                                  /* released */
        /* joystick 2 keeps working so a second player is not interrupted */
        pad_state[1][RETRO_DEVICE_ID_JOYPAD_LEFT] = 1; frames(2);
        CHECK(COLBITS(6) == (0xFF & ~(1u << 1)));
        pad_state[1][RETRO_DEVICE_ID_JOYPAD_LEFT] = 0; frames(2);
        /* cursor moves with the D-pad: right -> 'S' */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1; frames(1); pad_state[0][RETRO_DEVICE_ID_JOYPAD_RIGHT] = 0; frames(1);
        CHECK(COLBITS(4) == 0xFF);                                  /* D-pad moved the cursor, it did not move the joystick */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 1; frames(3); pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 0;
        CHECK(COLBITS(8) == (0xFF & ~(1u << 5)));                   /* 'S' = col 1 row 5 */
        frames(8);
        /* Start still presses ENTER while open */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_START] = 1; frames(2); CHECK(COLBITS(0) == (0xFF & ~(1u << 2)));
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_START] = 0; frames(2);
        /* F11 on a real keyboard also toggles it; close with Select and the game gets its joystick back */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = 1; frames(2); pad_state[0][RETRO_DEVICE_ID_JOYPAD_SELECT] = 0; frames(2);
        CHECK(memcmp(bottom_row, before, sizeof before) == 0);      /* panel gone */
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 1; frames(2);
        CHECK(COLBITS(4) == (0xFF & ~(1u << 0)));
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 0; frames(2);
        key(1, RETROK_F11); key(0, RETROK_F11); frames(2);
        CHECK(memcmp(bottom_row, before, sizeof before) != 0);
        key(1, RETROK_F11); key(0, RETROK_F11); frames(2);
        CHECK(memcmp(bottom_row, before, sizeof before) == 0);
        /* a key stuck down when the keyboard is closed must not stay pressed */
        key(1, RETROK_F11); key(0, RETROK_F11); frames(2);
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 1; frames(2);
        key(1, RETROK_F11); key(0, RETROK_F11); frames(1);
        pad_state[0][RETRO_DEVICE_ID_JOYPAD_B] = 0; frames(4);
        CHECK(COLBITS(2) == 0xFF && COLBITS(0) == 0xFF);
    }

    /* savestates */
    {
        size_t ss = c_ser_size();
        void *st = malloc(ss);
        CHECK(ss > 0x8000 && ss < 0x100000);
        CHECK(c_ser(st, ss));
        key(1, RETROK_a); frames(3);
        CHECK(COLBITS(2) != 0xFF);                                  /* RAM now shows the key as pressed */
        key(0, RETROK_a);
        CHECK(c_unser(st, ss));
        CHECK(COLBITS(2) == 0xFF);                                  /* restored RAM predates the key press */
        frames(2);
        CHECK(COLBITS(2) == 0xFF);
        CHECK(!c_unser(st, 10));                                    /* truncated state rejected */
        free(st);
    }

    /* reset and no-expansion option */
    option_expansion = "disabled";
    c_reset(); frames(2);
    mem = c_mem_data(RETRO_MEMORY_SYSTEM_RAM);
    CHECK(mem == NULL || xram(mem, 0) == 0);                       /* no expansion RAM: stores vanish */
    option_expansion = "enabled";
    c_unload(); c_deinit();

    /* ---- missing BIOS gives a clean failure ---- */
    snprintf(sysdir, sizeof sysdir, "/tmp/ti99_host_empty");
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", sysdir, sysdir); if (system(cmd)) return 2;
    c_init();
    CHECK(!c_load(NULL));
    c_deinit();

    /* ---- speech synthesizer ROM: valid, corrupt, option disabled; boot and savestates keep working ---- */
    {
        static uint8_t u2a[0x4000], u2b[0x4000];
        const char *nm[2] = { "cd2325a.u2a", "cd2326a.u2b" };
        const uint8_t *dt[2] = { u2a, u2b }; uint32_t sizes[2] = { 0x4000, 0x4000 };
        size_t zs, i, ss; uint8_t *z; void *st;
        snprintf(sysdir, sizeof sysdir, "/tmp/ti99_host_sys");
        for (i = 0; i < 0x4000; i++) { u2a[i] = (uint8_t)(i * 3); u2b[i] = (uint8_t)(i * 5); }
        z = mkzip(nm, dt, sizes, 2, &zs);
        snprintf(path, sizeof path, "%s/ti99_speech.zip", sysdir); wfile(path, z, zs); free(z);
        c_init();
        CHECK(c_load(NULL));
        frames(3);
        ss = c_ser_size(); st = malloc(ss);
        CHECK(ss > 0x8000 && c_ser(st, ss)); frames(2); CHECK(c_unser(st, ss)); free(st);
        option_speech = "disabled"; c_reset(); frames(2);
        option_speech = "enabled"; c_reset(); frames(2);
        c_unload(); c_deinit();
        wfile(path, "this is not a zip file", 22);                   /* corrupt: warn, but still boot */
        c_init();
        CHECK(c_load(NULL)); frames(2);
        c_unload(); c_deinit();
        remove(path);
    }

    /* ---- cartridge zip loads and appears in the memory map ---- */
    snprintf(sysdir, sizeof sysdir, "/tmp/ti99_host_sys");
    {
        static uint8_t cart[0x2000];
        const char *nm[1] = { "mygamec.bin" };
        const uint8_t *dt[1] = { cart }; uint32_t sizes[1] = { 0x2000 };
        size_t zs; uint8_t *z;
        memset(cart, 0x5A, sizeof cart);
        z = mkzip(nm, dt, sizes, 1, &zs);
        c_init();
        CHECK(load_content("mygame.zip", z, zs, 0));            /* as RetroArch does it: path only */
        frames(5);
        CHECK(frames_seen > 20);
        c_unload(); c_deinit();
        c_init();
        CHECK(load_content("mygame.zip", z, zs, 1));            /* other frontends: path + data buffer */
        frames(2);
        c_unload(); c_deinit();
        /* path to a file that does not exist: clean failure with a message, no crash */
        c_init();
        memset(&game, 0, sizeof game);
        game.path = "/tmp/ti99_host_content/does_not_exist.zip";
        CHECK(!c_load(&game));
        c_deinit();
        free(z);
    }

    /* optional: real cartridge zips given on the command line (host_test core.so bench|- zip...) */
    {
        int a;
        for (a = 3; a < argc; a++) {
            size_t n = 0; uint8_t *d = NULL;
            FILE *f = fopen(argv[a], "rb");
            if (!f) { printf("  cannot open %s\n", argv[a]); g_fail++; continue; }
            fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
            d = malloc(n); if (fread(d, 1, n, f) != n) { g_fail++; } fclose(f);
            c_init();
            printf("  cartridge %s\n", argv[a]);
            {   /* hand the core the real file path, exactly as RetroArch does with need_fullpath */
                struct retro_game_info g2;
                memset(&g2, 0, sizeof g2);
                g2.path = argv[a];
                if (!g_need_fullpath) { g2.data = d; g2.size = n; }
                CHECK(c_load(&g2));
            }
            frames(30);
            { size_t ss = c_ser_size(); void *st = malloc(ss); CHECK(c_ser(st, ss)); CHECK(c_unser(st, ss)); free(st); }
            c_unload(); c_deinit();
            free(d);
        }
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    dlclose(h);
    return g_fail ? 1 : 0;
}
