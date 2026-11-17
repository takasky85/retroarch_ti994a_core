// Minimal stand-in for MAME's emu.h: just enough to compile the unmodified
// tms5220.cpp / tms6100.cpp for differential testing against src/speech.c.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <vector>
#include <iterator>
#include <type_traits>

using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using s8 = int8_t; using s16 = int16_t; using s32 = int32_t; using s64 = int64_t;
using offs_t = uint32_t;
#define ATTR_COLD
#define ATTR_COLD_X
#define NAME(x) #x, x
#define FUNC(x) &x, #x
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)
#define DEVICE_SELF ""
#define DECLARE_DEVICE_TYPE(a, b) extern const device_type a;
#define DEFINE_DEVICE_TYPE(a, b, c, d) const device_type a = 0;
#define ASSERT_LINE 1
#define CLEAR_LINE 0
#define fatalerror(...) do { std::fprintf(stderr, __VA_ARGS__); std::abort(); } while (0)

using device_type = int;
struct machine_config {};
struct attotime {
    static attotime from_usec(int) { return {}; }
};
inline attotime clocks_to_attotime(int) { return {}; }
struct emu_timer { void adjust(attotime, int = 0) {} };
struct sound_stream {
    void update() {}
    void set_sample_rate(int) {}
    int samples() const { return 0; }
    void put_int(int, int, int, int) {}
};
struct running_machine { bool side_effects_disabled() const { return false; } };

template <typename T> inline T bitswap8(T) = delete;
template <int N, typename T> inline u8 bitswap(T v, int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0) {
    static_assert(N == 8, "only 8 bit");
    u8 r = 0; int src[8] = {b7, b6, b5, b4, b3, b2, b1, b0};
    for (int i = 0; i < 8; i++) r |= ((v >> src[i]) & 1) << (7 - i);
    return (u8)r;
}

class device_t {
public:
    device_t(const machine_config &, device_type, const char *, device_t *, uint32_t clk) : m_clock(clk) {}
    virtual ~device_t() {}
    uint32_t clock() const { return m_clock; }
    running_machine &machine() { return m_machine; }
    void reset() { device_reset(); }
    template <typename... Ts> void save_item(Ts &&...) {}
    template <typename F, typename... Ts> emu_timer *timer_alloc(F &&, const char *, Ts &&...) { return &m_timer; }
    virtual void device_start() {}
    virtual void device_reset() {}
    virtual void device_clock_changed() {}
protected:
    uint32_t m_clock;
    running_machine m_machine;
    emu_timer m_timer;
};

class device_sound_interface {
public:
    device_sound_interface(const machine_config &, device_t &) {}
    virtual ~device_sound_interface() {}
    sound_stream *stream_alloc(int, int, int) { return &m_stream; }
    virtual void sound_stream_update(sound_stream &) = 0;
private:
    sound_stream m_stream;
};

template <typename T> class required_region_ptr {
public:
    required_region_ptr(device_t &, const char *) {}
    void set(T *p, size_t n) { m_p = p; m_n = n; }
    size_t bytes() const { return m_n * sizeof(T); }
    T &operator[](size_t i) const { return m_p[i]; }
private:
    T *m_p = nullptr; size_t m_n = 0;
};

// devcb stand-ins: callbacks are plain std::function members assigned by the harness
struct devcb_write_line {
    std::function<void(int)> fn;
    devcb_write_line(device_t &) {}
    devcb_write_line &bind() { return *this; }
    bool isunset() const { return !fn; }
    void operator()(int v) const { if (fn) fn(v); }
};
struct devcb_write8 {
    std::function<void(offs_t, u8)> fn;
    devcb_write8(device_t &) {}
    devcb_write8 &bind() { return *this; }
    bool isunset() const { return !fn; }
    void operator()(offs_t o, u8 v) const { if (fn) fn(o, v); }
};
struct devcb_read_line {
    std::function<int()> fn;
    devcb_read_line(device_t &, int) {}
    devcb_read_line &bind() { return *this; }
    bool isunset() const { return !fn; }
    int operator()() const { return fn ? fn() : 0; }
};
