/* libretro front-end glue for the TI-99/4A core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"
#include "ti99.h"
#include "loader.h"
#include "keymatrix.h"
#include "speech.h"
#include "osk.h"

#define CORE_NAME    "TI-99/4A"
#define CORE_VERSION "0.2.1"
#define SAMPLE_RATE  48000

/* ------------------------------------------------------------ callbacks */

static retro_environment_t        env_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;
static retro_log_printf_t         log_cb;

static ti99 *machine;
static char  system_dir[1024];
static char  content_dir[1024];
static int   console_loaded;
static uint8_t console_rom[0x2000], console_grom[0x6000];
static int16_t audio_buf[TI99_MAX_AUDIO_FRAMES * 2];
static uint8_t speech_rom[SPEECH_ROM_SIZE];
static osk     keyboard;                    /* on-screen keyboard */
static uint32_t osk_fb[VDP_WIDTH * VDP_HEIGHT];
static uint8_t osk_key_request;             /* physical F11 pressed */

static void logmsg(enum retro_log_level lvl, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (log_cb) log_cb(lvl, "[ti99] %s\n", buf);
    else fprintf(stderr, "[ti99] %s\n", buf);
}

static void show_message(const char *msg, unsigned frames)
{
    struct retro_message m;
    m.msg = msg;
    m.frames = frames;
    if (env_cb) env_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
}

/* ------------------------------------------------------- keyboard mapping */

#define M_SH 1   /* also hold TI SHIFT */
#define M_FC 2   /* also hold TI FCTN  */
#define M_NS 4   /* TI SHIFT must be released */

typedef struct { uint8_t key, mod, skey, smod; } kmap_t;
#define NKEYS 512
static kmap_t kmap[NKEYS];
static uint8_t held[NKEYS];
static uint8_t alpha_lock;

static void km(unsigned code, uint8_t key, uint8_t mod, uint8_t skey, uint8_t smod)
{
    if (code < NKEYS) { kmap[code].key = key; kmap[code].mod = mod; kmap[code].skey = skey; kmap[code].smod = smod; }
}
static void km1(unsigned code, uint8_t key) { km(code, key, 0, key, 0); }

static void build_keymap(void)
{
    static const uint8_t letters[26] = {
        K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
        K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z };
    static const uint8_t digits[10] = { K_0, K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9 };
    unsigned i;

    for (i = 0; i < NKEYS; i++) { kmap[i].key = kmap[i].skey = K_NONE; kmap[i].mod = kmap[i].smod = 0; }
    for (i = 0; i < 26; i++) km1(RETROK_a + i, letters[i]);
    for (i = 0; i < 10; i++) { km1(RETROK_0 + i, digits[i]); km1(RETROK_KP0 + i, digits[i]); }

    km1(RETROK_SPACE, K_SPACE);   km1(RETROK_RETURN, K_ENTER); km1(RETROK_KP_ENTER, K_ENTER);
    km1(RETROK_COMMA, K_COMMA);   km1(RETROK_PERIOD, K_PERIOD); km1(RETROK_KP_PERIOD, K_PERIOD);
    km1(RETROK_SEMICOLON, K_SEMI); km1(RETROK_EQUALS, K_EQ);
    km(RETROK_SLASH, K_SLASH, 0, K_I, M_FC | M_NS);                  /* ?  = FCTN I */
    km(RETROK_KP_DIVIDE, K_SLASH, 0, K_SLASH, 0);
    km(RETROK_MINUS, K_SLASH, M_SH, K_U, M_FC | M_NS);               /* -  = SHIFT /, _ = FCTN U */
    km(RETROK_KP_MINUS, K_SLASH, M_SH, K_SLASH, M_SH);
    km(RETROK_KP_PLUS, K_EQ, M_SH, K_EQ, M_SH);
    km(RETROK_KP_MULTIPLY, K_8, M_SH, K_8, M_SH);
    km(RETROK_QUOTE, K_O, M_FC | M_NS, K_P, M_FC | M_NS);            /* '  = FCTN O, " = FCTN P */
    km(RETROK_LEFTBRACKET, K_R, M_FC | M_NS, K_F, M_FC | M_NS);      /* [ ] { } */
    km(RETROK_RIGHTBRACKET, K_T, M_FC | M_NS, K_G, M_FC | M_NS);
    km(RETROK_BACKSLASH, K_Z, M_FC | M_NS, K_A, M_FC | M_NS);        /* \ | */
    km(RETROK_BACKQUOTE, K_C, M_FC | M_NS, K_W, M_FC | M_NS);        /* ` ~ */

    km(RETROK_BACKSPACE, K_S, M_FC | M_NS, K_S, M_FC | M_NS);
    km(RETROK_LEFT, K_S, M_FC | M_NS, K_S, M_FC | M_NS);
    km(RETROK_RIGHT, K_D, M_FC | M_NS, K_D, M_FC | M_NS);
    km(RETROK_UP, K_E, M_FC | M_NS, K_E, M_FC | M_NS);
    km(RETROK_DOWN, K_X, M_FC | M_NS, K_X, M_FC | M_NS);
    km(RETROK_DELETE, K_1, M_FC | M_NS, K_1, M_FC | M_NS);           /* DEL */
    km(RETROK_INSERT, K_2, M_FC | M_NS, K_2, M_FC | M_NS);           /* INS */
    km(RETROK_ESCAPE, K_9, M_FC | M_NS, K_9, M_FC | M_NS);           /* BACK */
}

static void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t mods)
{
    (void)character; (void)mods;
    if (keycode >= NKEYS) return;
    if (keycode == RETROK_CAPSLOCK) { if (down) alpha_lock ^= 1u; return; }
    if (keycode == RETROK_F11) { if (down) osk_key_request = 1; return; }
    held[keycode] = down ? 1u : 0u;
}

static void setkey(uint8_t keys[8], uint8_t k) { if (k != K_NONE) keys[k >> 3] |= (uint8_t)(1u << (k & 7u)); }

static int pad(unsigned port, unsigned id) { return input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id) != 0; }

static void poll_input(uint8_t keys[8])
{
    int shift, ctrl, fctn, f_sh, f_fc, f_ns = 0, any_sh = 0;
    unsigned i, port;

    memset(keys, 0, 8);
    input_poll_cb();

    shift = held[RETROK_LSHIFT] || held[RETROK_RSHIFT];
    ctrl  = held[RETROK_LCTRL]  || held[RETROK_RCTRL];
    fctn  = held[RETROK_LALT]   || held[RETROK_RALT];
    f_sh = shift; f_fc = fctn;

    for (i = 0; i < NKEYS; i++) {
        const kmap_t *e;
        uint8_t key, mod;
        if (!held[i]) continue;
        e = &kmap[i];
        key = shift ? e->skey : e->key;
        mod = shift ? e->smod : e->mod;
        if (key == K_NONE) continue;
        setkey(keys, key);
        if (mod & M_SH) { f_sh = 1; any_sh = 1; }
        if (mod & M_FC) f_fc = 1;
        if (mod & M_NS) f_ns = 1;
    }

    {
        /* pad 1 drives the on-screen keyboard; while it is open the pad no longer reaches the game */
        int16_t lx = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int16_t ly = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        unsigned ob = 0;
        int alpha_toggle = 0;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_UP)    || ly < -0x4000) ob |= OSK_UP;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_DOWN)  || ly >  0x4000) ob |= OSK_DOWN;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_LEFT)  || lx < -0x4000) ob |= OSK_LEFT;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_RIGHT) || lx >  0x4000) ob |= OSK_RIGHT;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_B))      ob |= OSK_PRESS;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_A))      ob |= OSK_CLOSE;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_Y))      ob |= OSK_MOVE;
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_SELECT) || osk_key_request) ob |= OSK_TOGGLE;
        osk_key_request = 0;
        osk_frame(&keyboard, ob, keys, &alpha_toggle);
        if (alpha_toggle) alpha_lock ^= 1u;
    }

    if (!osk_is_open(&keyboard)) {
        for (port = 0; port < 2; port++) {
            uint8_t joy = 0;
            int16_t ax = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
            int16_t ay = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
            if (pad(port, RETRO_DEVICE_ID_JOYPAD_B) || pad(port, RETRO_DEVICE_ID_JOYPAD_A)) joy |= 1u << 0;
            if (pad(port, RETRO_DEVICE_ID_JOYPAD_LEFT)  || ax < -0x4000) joy |= 1u << 1;
            if (pad(port, RETRO_DEVICE_ID_JOYPAD_RIGHT) || ax >  0x4000) joy |= 1u << 2;
            if (pad(port, RETRO_DEVICE_ID_JOYPAD_DOWN)  || ay >  0x4000) joy |= 1u << 3;
            if (pad(port, RETRO_DEVICE_ID_JOYPAD_UP)    || ay < -0x4000) joy |= 1u << 4;
            keys[6 + port] |= joy;
        }
        /* Convenience keys on pad 1 so the console is usable without a keyboard (e.g. Android TV).
         * Select opens the on-screen keyboard (SPACE and everything else is on it). */
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_Y))      setkey(keys, K_SPACE);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_L))      setkey(keys, K_1);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_R))      setkey(keys, K_2);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_L2))     setkey(keys, K_3);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_R2))     setkey(keys, K_4);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_X))      setkey(keys, K_Y);
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_L3)) { setkey(keys, K_EQ); f_fc = 1; }   /* FCTN = : QUIT */
        if (pad(0, RETRO_DEVICE_ID_JOYPAD_R3)) { setkey(keys, K_9);  f_fc = 1; }   /* FCTN 9 : BACK */
    } else {
        /* the second joystick stays live so a second player is not interrupted */
        int16_t ax = input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int16_t ay = input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        uint8_t joy = 0;
        if (pad(1, RETRO_DEVICE_ID_JOYPAD_B) || pad(1, RETRO_DEVICE_ID_JOYPAD_A)) joy |= 1u << 0;
        if (pad(1, RETRO_DEVICE_ID_JOYPAD_LEFT)  || ax < -0x4000) joy |= 1u << 1;
        if (pad(1, RETRO_DEVICE_ID_JOYPAD_RIGHT) || ax >  0x4000) joy |= 1u << 2;
        if (pad(1, RETRO_DEVICE_ID_JOYPAD_DOWN)  || ay >  0x4000) joy |= 1u << 3;
        if (pad(1, RETRO_DEVICE_ID_JOYPAD_UP)    || ay < -0x4000) joy |= 1u << 4;
        keys[7] |= joy;
    }
    if (pad(0, RETRO_DEVICE_ID_JOYPAD_START))  setkey(keys, K_ENTER);

    if (f_ns && !any_sh) f_sh = 0;
    if (f_sh) setkey(keys, K_SHIFT);
    if (f_fc) setkey(keys, K_FCTN);
    if (ctrl) setkey(keys, K_CTRL);
}

/* -------------------------------------------------------------- options */

static int get_option_int(const char *key, const char *match, int if_match, int otherwise)
{
    struct retro_variable v;
    v.key = key; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value)
        return strncmp(v.value, match, strlen(match)) == 0 ? if_match : otherwise;
    return otherwise;
}

static void apply_options(void)
{
    int exp = get_option_int("ti99_expansion", "enabled", 1, 0);
    int spr = get_option_int("ti99_sprite_limit", "32", 32, 4);
    if (machine) {
        ti99_set_options(machine, exp, spr);
        ti99_set_speech_enabled(machine, get_option_int("ti99_speech", "disabled", 0, 1));
    }
}

static void apply_live_options(void)
{
    /* the 32K expansion only changes on reset; the sprite limit can change immediately */
    if (machine) {
        ti99_set_sprite_limit(machine, get_option_int("ti99_sprite_limit", "32", 32, 4));
        ti99_set_speech_enabled(machine, get_option_int("ti99_speech", "disabled", 0, 1));
    }
}

/* --------------------------------------------------------------- libretro */

void retro_set_environment(retro_environment_t cb)
{
    static const struct retro_variable vars[] = {
        { "ti99_expansion",    "32K RAM expansion (restart required); enabled|disabled" },
        { "ti99_sprite_limit", "Sprites per scanline; 4 (original hardware)|32 (no flicker)" },
        { "ti99_speech",       "Speech synthesizer (needs ti99_speech.zip); enabled|disabled" },
        { NULL, NULL }
    };
    static const struct retro_controller_description pads[] = {
        { "Joystick + convenience keys", RETRO_DEVICE_JOYPAD },
    };
    static const struct retro_controller_info ports[] = { { pads, 1 }, { pads, 1 }, { NULL, 0 } };
    bool no_game = true;
    struct retro_log_callback logging;

    env_cb = cb;
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) log_cb = logging.log;
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
    const char *dir = NULL;
    build_keymap();
    osk_init(&keyboard);
    system_dir[0] = 0;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
        snprintf(system_dir, sizeof system_dir, "%s", dir);
    machine = ti99_create(SAMPLE_RATE);
}

void retro_deinit(void)
{
    ti99_destroy(machine);
    machine = NULL;
    console_loaded = 0;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof *info);
    info->library_name = CORE_NAME;
    info->library_version = CORE_VERSION;
    info->valid_extensions = "zip|rpk|bin|c|d|g";
    /* Like the arcade cores: ask the frontend for the PATH only and never to extract the archive.
     * MAME cartridge zips hold several files, which the core reads and lays out itself.  (With
     * need_fullpath = false, RetroArch 1.22 tried to read the zip into memory itself and failed
     * with "Could not read content file" before this core was even called.) */
    info->need_fullpath = true;
    info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof *info);
    info->geometry.base_width = VDP_WIDTH;
    info->geometry.base_height = VDP_HEIGHT;
    info->geometry.max_width = VDP_WIDTH;
    info->geometry.max_height = VDP_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = TI99_FPS;
    info->timing.sample_rate = SAMPLE_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    char err[512];
    static const struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Joystick Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Joystick Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Joystick Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Joystick Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Fire" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Fire (alt)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Key: ENTER" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "On-screen keyboard" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Key: SPACE" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Key: 1" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Key: 2" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Key: 3" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Key: 4" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Key: Y (yes)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "FCTN+= (QUIT)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "FCTN+9 (BACK)" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Joystick Left" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Joystick Up" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Joystick Down" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Joystick Right" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Fire" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Fire (alt)" },
        { 0, 0, 0, 0, NULL }
    };
    struct retro_keyboard_callback kb;

    if (!machine) return false;
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)desc);
    kb.callback = keyboard_event;
    env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
    memset(held, 0, sizeof held);
    alpha_lock = 0;
    osk_init(&keyboard);
    osk_key_request = 0;

    content_dir[0] = 0;
    if (game && game->path) {
        const char *p, *last = NULL;
        for (p = game->path; *p; p++) if (*p == '/' || *p == '\\') last = p;
        if (last) {
            size_t n = (size_t)(last - game->path);
            if (n >= sizeof content_dir) n = sizeof content_dir - 1;
            memcpy(content_dir, game->path, n);
            content_dir[n] = 0;
        }
    }

    if (loader_load_console(system_dir, content_dir, console_rom, console_grom, err, sizeof err) != 0) {
        logmsg(RETRO_LOG_ERROR, "%s", err);
        show_message(err, 600);
        return false;
    }
    console_loaded = 1;
    ti99_set_console(machine, console_rom, console_grom);
    ti99_remove_cart(machine);

    {   /* speech synthesizer: optional, enabled by default when MAME's ti99_speech.zip is present */
        char note[128] = "";
        int sr = loader_load_speech(system_dir, content_dir, speech_rom, note, sizeof note, err, sizeof err);
        if (sr == 0) {
            ti99_set_speech_rom(machine, speech_rom);
            logmsg(RETRO_LOG_INFO, "%s", note);
        } else {
            ti99_set_speech_rom(machine, NULL);
            if (sr < 0) logmsg(RETRO_LOG_WARN, "%s", err);
            else logmsg(RETRO_LOG_INFO, "speech synthesizer unavailable: %s (put MAME's ti99_speech.zip in the system folder)", err);
        }
    }

    if (game && ((game->data && game->size) || (game->path && game->path[0]))) {
        loaded_cart lc;
        const uint8_t *cdata = (const uint8_t *)game->data;
        size_t csize = game->size;
        uint8_t *owned = NULL;
        if (!cdata || !csize) {                         /* path-only: read the file ourselves */
            owned = loader_read_file(game->path, &csize);
            if (!owned || !csize) {
                snprintf(err, sizeof err, "cannot read cartridge file: %s", game->path);
                free(owned);
                logmsg(RETRO_LOG_ERROR, "%s", err);
                show_message(err, 600);
                return false;
            }
            cdata = owned;
        }
        if (loader_load_cart(game->path, cdata, csize, system_dir, &lc, err, sizeof err) != 0) {
            free(owned);
            logmsg(RETRO_LOG_ERROR, "cartridge: %s", err);
            show_message(err, 600);
            return false;
        }
        free(owned);
        if (ti99_insert_cart(machine, &lc.desc) != 0) {
            loaded_cart_free(&lc);
            logmsg(RETRO_LOG_ERROR, "out of memory loading cartridge");
            return false;
        }
        logmsg(RETRO_LOG_INFO, "cartridge loaded: type %d, ROM %lu bytes, %d GROM block(s), %s",
               lc.desc.type, (unsigned long)lc.desc.rom_size, lc.desc.ngrom, lc.note);
        if (strncmp(lc.note, "guessed", 7) == 0)
            show_message("TI-99: cartridge not in the MAME software list; layout guessed from file names", 480);
        loaded_cart_free(&lc);
    } else {
        logmsg(RETRO_LOG_INFO, "starting without a cartridge");
    }

    apply_options();
    ti99_reset(machine);
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    (void)type; (void)info; (void)num;
    return false;
}

void retro_unload_game(void)
{
    if (machine) ti99_remove_cart(machine);
}

void retro_reset(void)
{
    if (machine && console_loaded) { apply_options(); ti99_reset(machine); }
}

void retro_run(void)
{
    uint8_t keys[8];
    int n = 0;
    bool updated = false;

    if (!machine || !console_loaded) return;
    if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) apply_live_options();
    poll_input(keys);
    ti99_set_keys(machine, keys, alpha_lock);
    ti99_run_frame(machine, audio_buf, &n);
    if (osk_is_open(&keyboard)) {
        memcpy(osk_fb, ti99_framebuffer(machine), sizeof osk_fb);
        osk_draw(&keyboard, osk_fb, alpha_lock);
        video_cb(osk_fb, VDP_WIDTH, VDP_HEIGHT, VDP_WIDTH * sizeof(uint32_t));
    } else {
        video_cb(ti99_framebuffer(machine), VDP_WIDTH, VDP_HEIGHT, VDP_WIDTH * sizeof(uint32_t));
    }
    if (n > 0) audio_batch_cb(audio_buf, (size_t)n);
}

size_t retro_serialize_size(void) { return ti99_state_size(); }
bool retro_serialize(void *data, size_t size) { return machine && ti99_save_state(machine, data, size) != 0; }
bool retro_unserialize(const void *data, size_t size) { return machine && ti99_load_state(machine, data, size) != 0; }

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void *retro_get_memory_data(unsigned id)
{
    size_t n;
    if (!machine) return NULL;
    if ((id & RETRO_MEMORY_MASK) == RETRO_MEMORY_SAVE_RAM) return ti99_cart_ram(machine, &n);
    if ((id & RETRO_MEMORY_MASK) == RETRO_MEMORY_SYSTEM_RAM) return ti99_expansion_ram(machine, &n);
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    size_t n = 0;
    if (!machine) return 0;
    if ((id & RETRO_MEMORY_MASK) == RETRO_MEMORY_SAVE_RAM) { ti99_cart_ram(machine, &n); return n; }
    if ((id & RETRO_MEMORY_MASK) == RETRO_MEMORY_SYSTEM_RAM) { ti99_expansion_ram(machine, &n); return n; }
    return 0;
}
