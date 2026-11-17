/* Minimal subset of libretro.h (MIT licensed API) -- only what this core uses.
 *
 * This is a STAND-IN so the project builds with no network access. For
 * submission to the libretro buildbot, delete this file and use the official
 * libretro.h from https://github.com/libretro/libretro-common (include/).
 * Every constant below matches the official header.
 */
#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#if defined(_MSC_VER) && _MSC_VER < 1800 && !defined(SN_TARGET_PS3)
#define bool unsigned char
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif
#endif

#ifndef RETRO_CALLCONV
#define RETRO_CALLCONV
#endif

#ifndef RETRO_API
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#ifdef RETRO_IMPORT_SYMBOLS
#define RETRO_API RETRO_CALLCONV __declspec(dllimport)
#else
#define RETRO_API RETRO_CALLCONV __declspec(dllexport)
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define RETRO_API RETRO_CALLCONV __attribute__((__visibility__("default")))
#else
#define RETRO_API RETRO_CALLCONV
#endif
#endif
#endif

#define RETRO_API_VERSION 1

#define RETRO_DEVICE_TYPE_SHIFT 8
#define RETRO_DEVICE_MASK       ((1 << RETRO_DEVICE_TYPE_SHIFT) - 1)
#define RETRO_DEVICE_NONE       0
#define RETRO_DEVICE_JOYPAD     1
#define RETRO_DEVICE_MOUSE      2
#define RETRO_DEVICE_KEYBOARD   3
#define RETRO_DEVICE_LIGHTGUN   4
#define RETRO_DEVICE_ANALOG     5
#define RETRO_DEVICE_POINTER    6

#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L       10
#define RETRO_DEVICE_ID_JOYPAD_R       11
#define RETRO_DEVICE_ID_JOYPAD_L2      12
#define RETRO_DEVICE_ID_JOYPAD_R2      13
#define RETRO_DEVICE_ID_JOYPAD_L3      14
#define RETRO_DEVICE_ID_JOYPAD_R3      15

#define RETRO_DEVICE_INDEX_ANALOG_LEFT   0
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT  1
#define RETRO_DEVICE_ID_ANALOG_X         0
#define RETRO_DEVICE_ID_ANALOG_Y         1

#define RETRO_REGION_NTSC  0
#define RETRO_REGION_PAL   1

#define RETRO_MEMORY_MASK        0xff
#define RETRO_MEMORY_SAVE_RAM    0
#define RETRO_MEMORY_RTC         1
#define RETRO_MEMORY_SYSTEM_RAM  2
#define RETRO_MEMORY_VIDEO_RAM   3

enum retro_log_level
{
   RETRO_LOG_DEBUG = 0,
   RETRO_LOG_INFO,
   RETRO_LOG_WARN,
   RETRO_LOG_ERROR,
   RETRO_LOG_DUMMY = INT_MAX
};

enum retro_pixel_format
{
   RETRO_PIXEL_FORMAT_0RGB1555 = 0,
   RETRO_PIXEL_FORMAT_XRGB8888 = 1,
   RETRO_PIXEL_FORMAT_RGB565   = 2,
   RETRO_PIXEL_FORMAT_UNKNOWN  = INT_MAX
};

#define RETRO_ENVIRONMENT_SET_MESSAGE             6
#define RETRO_ENVIRONMENT_SHUTDOWN                7
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY    9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT       10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS  11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK  12
#define RETRO_ENVIRONMENT_GET_VARIABLE           15
#define RETRO_ENVIRONMENT_SET_VARIABLES          16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE    17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME    18
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE      27
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY     31
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO    35
#define RETRO_ENVIRONMENT_SET_GEOMETRY           37

struct retro_message
{
   const char *msg;
   unsigned frames;
};

struct retro_input_descriptor
{
   unsigned port;
   unsigned device;
   unsigned index;
   unsigned id;
   const char *description;
};

struct retro_variable
{
   const char *key;
   const char *value;
};

typedef void (RETRO_CALLCONV *retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
struct retro_log_callback
{
   retro_log_printf_t log;
};

typedef void (RETRO_CALLCONV *retro_keyboard_event_t)(bool down, unsigned keycode,
      uint32_t character, uint16_t key_modifiers);
struct retro_keyboard_callback
{
   retro_keyboard_event_t callback;
};

struct retro_controller_description
{
   const char *desc;
   unsigned id;
};

struct retro_controller_info
{
   const struct retro_controller_description *types;
   unsigned num_types;
};

struct retro_game_info
{
   const char *path;
   const void *data;
   size_t      size;
   const char *meta;
};

struct retro_system_info
{
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   bool        need_fullpath;
   bool        block_extract;
};

struct retro_game_geometry
{
   unsigned base_width;
   unsigned base_height;
   unsigned max_width;
   unsigned max_height;
   float    aspect_ratio;
};

struct retro_system_timing
{
   double fps;
   double sample_rate;
};

struct retro_system_av_info
{
   struct retro_game_geometry geometry;
   struct retro_system_timing timing;
};

typedef bool    (RETRO_CALLCONV *retro_environment_t)(unsigned cmd, void *data);
typedef void    (RETRO_CALLCONV *retro_video_refresh_t)(const void *data, unsigned width,
      unsigned height, size_t pitch);
typedef void    (RETRO_CALLCONV *retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t  (RETRO_CALLCONV *retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void    (RETRO_CALLCONV *retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV *retro_input_state_t)(unsigned port, unsigned device,
      unsigned index, unsigned id);

RETRO_API void retro_set_environment(retro_environment_t);
RETRO_API void retro_set_video_refresh(retro_video_refresh_t);
RETRO_API void retro_set_audio_sample(retro_audio_sample_t);
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
RETRO_API void retro_set_input_poll(retro_input_poll_t);
RETRO_API void retro_set_input_state(retro_input_state_t);
RETRO_API void retro_init(void);
RETRO_API void retro_deinit(void);
RETRO_API unsigned retro_api_version(void);
RETRO_API void retro_get_system_info(struct retro_system_info *info);
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info);
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device);
RETRO_API void retro_reset(void);
RETRO_API void retro_run(void);
RETRO_API size_t retro_serialize_size(void);
RETRO_API bool retro_serialize(void *data, size_t size);
RETRO_API bool retro_unserialize(const void *data, size_t size);
RETRO_API void retro_cheat_reset(void);
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code);
RETRO_API bool retro_load_game(const struct retro_game_info *game);
RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info,
      size_t num_info);
RETRO_API void retro_unload_game(void);
RETRO_API unsigned retro_get_region(void);
RETRO_API void *retro_get_memory_data(unsigned id);
RETRO_API size_t retro_get_memory_size(unsigned id);

/* Keyboard codes (identical to the official header; SDL 1.2 style values). */
enum retro_key
{
   RETROK_UNKNOWN = 0,
   RETROK_BACKSPACE = 8, RETROK_TAB = 9, RETROK_RETURN = 13, RETROK_ESCAPE = 27,
   RETROK_SPACE = 32, RETROK_QUOTE = 39, RETROK_COMMA = 44, RETROK_MINUS = 45,
   RETROK_PERIOD = 46, RETROK_SLASH = 47,
   RETROK_0 = 48, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5, RETROK_6,
   RETROK_7, RETROK_8, RETROK_9,
   RETROK_SEMICOLON = 59, RETROK_EQUALS = 61,
   RETROK_LEFTBRACKET = 91, RETROK_BACKSLASH = 92, RETROK_RIGHTBRACKET = 93,
   RETROK_BACKQUOTE = 96,
   RETROK_a = 97, RETROK_b, RETROK_c, RETROK_d, RETROK_e, RETROK_f, RETROK_g,
   RETROK_h, RETROK_i, RETROK_j, RETROK_k, RETROK_l, RETROK_m, RETROK_n,
   RETROK_o, RETROK_p, RETROK_q, RETROK_r, RETROK_s, RETROK_t, RETROK_u,
   RETROK_v, RETROK_w, RETROK_x, RETROK_y, RETROK_z,
   RETROK_DELETE = 127,
   RETROK_KP0 = 256, RETROK_KP1, RETROK_KP2, RETROK_KP3, RETROK_KP4, RETROK_KP5,
   RETROK_KP6, RETROK_KP7, RETROK_KP8, RETROK_KP9,
   RETROK_KP_PERIOD = 266, RETROK_KP_DIVIDE = 267, RETROK_KP_MULTIPLY = 268,
   RETROK_KP_MINUS = 269, RETROK_KP_PLUS = 270, RETROK_KP_ENTER = 271,
   RETROK_UP = 273, RETROK_DOWN = 274, RETROK_RIGHT = 275, RETROK_LEFT = 276,
   RETROK_INSERT = 277,
   RETROK_RSHIFT = 303, RETROK_LSHIFT = 304, RETROK_RCTRL = 305, RETROK_LCTRL = 306,
   RETROK_RALT = 307, RETROK_LALT = 308,
   RETROK_CAPSLOCK = 301,
   RETROK_F11 = 292,
   RETROK_LAST = 323,
   RETROK_DUMMY = INT_MAX
};

#ifdef __cplusplus
}
#endif

#endif
