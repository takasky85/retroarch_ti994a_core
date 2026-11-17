// Lockstep differential test: MAME's real tms5220.cpp+tms6100.cpp (via shim) vs src/speech.c
#include "mame_refs.cpp"
extern "C" {
#include "/home/claude/ti99core/src/speech.h"
}
#include <cinttypes>

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static uint32_t rn(uint32_t n) { return rnd() % n; }

// ---- LPC bitstream encoder: fields MSB first, stream bits LSB-first within each byte ----
struct BitW {
    std::vector<uint8_t> b; size_t nbits = 0;
    void bit(int v) { if ((nbits & 7) == 0) b.push_back(0); if (v) b.back() |= (uint8_t)(1u << (nbits & 7)); nbits++; }
    void put(unsigned v, int n) { for (int i = n - 1; i >= 0; i--) bit((v >> i) & 1); }
};
static const int KB[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
static void enc_frame(BitW &w, int mode) {
    // mode bias: 0 normal, 1 allow stop at end
    unsigned e = 1 + rn(14);
    if (rn(10) == 0) e = 0;                          // silence frame
    w.put(e, 4);
    if (e == 0) return;
    int rep = rn(5) == 0;
    unsigned p = rn(4) == 0 ? 0 : 1 + rn(63);        // unvoiced sometimes
    w.put(rep, 1); w.put(p, 6);
    if (rep) return;
    for (int i = 0; i < 4; i++) w.put(rn(1u << KB[i]), KB[i]);
    if (p == 0) return;
    for (int i = 4; i < 10; i++) w.put(rn(1u << KB[i]), KB[i]);
    (void)mode;
}
static std::vector<uint8_t> make_utterance(int nframes, bool stop) {
    BitW w;
    for (int i = 0; i < nframes; i++) enc_frame(w, 0);
    if (stop) w.put(15, 4);
    return w.b;
}

struct Rig {
    tms5200_device *m; tms6100_device *vsm; speech s; std::vector<uint8_t> rom;
    machine_config mc; int ops = 0; long samples = 0;
    Rig() {
        rom.assign(SPEECH_ROM_SIZE, 0xFF);
        m = new tms5200_device(mc, "m", nullptr, 640000);
        vsm = new tms6100_device(mc, "v", nullptr, 0);
    }
    void setup() {
        vsm->m_rom.set(rom.data(), rom.size());
        vsm->device_start();
        m->m_m0_cb.fn = [this](int v) { vsm->m0_w(v); };
        m->m_m1_cb.fn = [this](int v) { vsm->m1_w(v); };
        m->m_addr_cb.fn = [this](offs_t, u8 d) { vsm->add_w(d); };
        m->m_data_cb.fn = [this]() { return vsm->data_line_r(); };
        m->m_romclk_cb.fn = [this](int v) { vsm->clk_w(v); };
        m->device_start();
        m->device_reset();
        speech_init(&s, rom.data());
    }
};

#define CHK(name, a, b) do { long _a = (long)(a), _b = (long)(b); if (_a != _b) { \
    std::printf("MISMATCH op %d sample %ld: %s mame=%ld mine=%ld\n", r.ops, r.samples, name, _a, _b); std::exit(1); } } while (0)

static void compare(Rig &r) {
    tms5220_device &m = *r.m; speech &s = r.s;
    CHK("talk_status", m.talk_status(), speech_talking(&s));
    CHK("SPEN", m.m_SPEN, s.spen); CHK("DDIS", m.m_DDIS, s.ddis);
    CHK("TALK", m.m_TALK, s.talk); CHK("TALKD", m.m_TALKD, s.talkd);
    CHK("BL", m.m_buffer_low, s.buffer_low); CHK("BE", m.m_buffer_empty, s.buffer_empty);
    CHK("fifo_count", m.m_fifo_count, s.fifo_count);
    CHK("fifo_head", m.m_fifo_head, s.fifo_head); CHK("fifo_tail", m.m_fifo_tail, s.fifo_tail);
    CHK("fifo_bits", m.m_fifo_bits_taken, s.fifo_bits_taken);
    CHK("cmd_reg", m.m_command_register, s.cmd_reg);
    CHK("RDB", m.m_RDB_flag, s.rdb_flag); CHK("rbreg", m.m_read_byte_register, s.read_byte_reg);
    CHK("sched_dummy", m.m_schedule_dummy_read, s.sched_dummy);
    CHK("subcycle", m.m_subcycle, s.subcycle); CHK("PC", m.m_PC, s.pc); CHK("IP", m.m_IP, s.ip);
    CHK("OLDE", m.m_OLDE, s.olde); CHK("OLDP", m.m_OLDP, s.oldp);
    CHK("inhibit", m.m_inhibit, s.inhibit); CHK("zpar", m.m_zpar, s.zpar); CHK("uv_zpar", m.m_uv_zpar, s.uv_zpar);
    CHK("pitch_zero", m.m_pitch_zero, s.pitch_zero);
    CHK("new_e", m.m_new_frame_energy_idx, s.new_e); CHK("new_p", m.m_new_frame_pitch_idx, s.new_p);
    for (int i = 0; i < 10; i++) { CHK("new_k", m.m_new_frame_k_idx[i], s.new_k[i]); CHK("cur_k", m.m_current_k[i], s.cur_k[i]); }
    CHK("cur_energy", m.m_current_energy, s.cur_energy); CHK("cur_pitch", m.m_current_pitch, s.cur_pitch);
    CHK("prev_energy", m.m_previous_energy, s.prev_energy);
    CHK("rng", m.m_RNG, s.rng); CHK("pitch_count", m.m_pitch_count, s.pitch_count);
    for (int i = 0; i < 11; i++) CHK("u", m.m_u[i], s.u[i]);
    for (int i = 0; i < 10; i++) CHK("x", m.m_x[i], s.x[i]);
    CHK("vsm_addr", r.vsm->m_address, s.vsm_addr); CHK("vsm_count", r.vsm->m_count, s.vsm_count);
    CHK("vsm_prev", r.vsm->m_prev_cmd, s.vsm_prev_cmd); CHK("vsm_sa", r.vsm->m_sa, s.vsm_sa);
    CHK("vsm_data", r.vsm->m_data, s.vsm_data);
}

long g_talk=0, g_voiced=0, g_unv=0, g_inh=0, g_maxabs=0, g_nonzero=0, g_stop=0, g_rep=0;
static void run_samples(Rig &r, int n) {
    for (int i = 0; i < n; i++) {
        int16_t buf[1];
        r.m->process(buf, 1);
        speech_advance(&r.s, SPEECH_CLOCKS_PER_SAMPLE);
        r.samples++;
        if (r.s.talkd) { g_talk++; if (r.s.oldp) g_unv++; else g_voiced++; if (r.s.inhibit) g_inh++; }
        { long a = buf[0] < 0 ? -(long)buf[0] : buf[0]; if (a > g_maxabs) g_maxabs = a; if (buf[0] != -1) g_nonzero++; }
        if (buf[0] != r.s.out_cur) { std::printf("MISMATCH op %d sample %ld: output mame=%d mine=%d\n", r.ops, r.samples, buf[0], r.s.out_cur); std::exit(1); }
        compare(r);
    }
}
long g_pending=0, g_fifofull=0;
static void wr(Rig &r, uint8_t v) {
    // keep inside the region where MAME's instant mode and my stall model agree
    int guard = 0;
    bool was_pending = r.m->m_command_register != 0xff;
    while ((r.m->m_command_register != 0xff || (r.m->m_DDIS && r.m->m_fifo_count >= 16)) && guard++ < 100000) run_samples(r, 1);
    if (guard) { if (was_pending) g_pending++; else g_fifofull++; }
    r.m->data_w(v);
    int stall = speech_write(&r.s, v);
    if (stall != 75) { std::printf("unexpected stall %d at op %d\n", stall, r.ops); std::exit(1); }
    compare(r);
}
static void rd(Rig &r) {
    uint8_t mine; speech_read(&r.s, &mine);
    uint8_t mame = r.m->status_r();
    CHK("status/data read", mame, mine);
    compare(r);
}
static void load_addr(Rig &r, unsigned addr, int nib) {
    for (int i = 0; i < nib; i++) wr(r, (uint8_t)(0x40 | ((addr >> (4 * i)) & 15)));
}

int main(int argc, char **argv) {
    long total_ops = argc > 1 ? atol(argv[1]) : 20000;
    if (argc > 2) rs ^= (uint64_t)atol(argv[2]) * 0x9E3779B97F4A7C15ull;
    Rig r; r.rom.assign(SPEECH_ROM_SIZE, 0xFF);
    // ROM: a pile of utterances at known addresses plus random junk between them
    std::vector<unsigned> starts;
    unsigned pos = 0;
    while (pos < SPEECH_ROM_SIZE - 600) {
        for (unsigned i = 0; i < 1 + rn(40) && pos < SPEECH_ROM_SIZE; i++) r.rom[pos++] = (uint8_t)rn(256);
        starts.push_back(pos);
        std::vector<uint8_t> u = make_utterance(3 + rn(60), rn(4) != 0);
        for (size_t i = 0; i < u.size() && pos < SPEECH_ROM_SIZE; i++) r.rom[pos++] = u[i];
    }
    r.setup();
    compare(r);
    long nstatus = 0, ntalk = 0, nspkext = 0, nrb = 0, nrdby = 0;
    for (long k = 0; k < total_ops; k++) {
        r.ops++;
        switch (rn(14)) {
        case 0: case 1: case 2: {                    // speak from VSM
            unsigned a = starts[rn((uint32_t)starts.size())];
            if (rn(8) == 0) a = rnd() & 0x7FFF;      // arbitrary (random junk) address
            load_addr(r, a, rn(10) == 0 ? 4 : 5);
            wr(r, 0x50);
            run_samples(r, 100 + rn(3000)); ntalk++;
            break; }
        case 3: {                                    // speak external, feed FIFO
            std::vector<uint8_t> u = make_utterance(2 + rn(25), rn(3) != 0);
            wr(r, 0x60); nspkext++;
            size_t i = 0; int safety = 0;
            while (i < u.size() && safety++ < 4000) {
                // write bytes while room, then let it play
                int burst = 1 + rn(10);
                while (burst-- && i < u.size()) {
                    if (r.m->m_fifo_count >= 16) break;
                    wr(r, u[i++]);
                }
                run_samples(r, rn(900));
                rd(r);
            }
            run_samples(r, 300 + rn(2500));
            break; }
        case 4: wr(r, 0x10); run_samples(r, rn(5)); rd(r); rd(r); nrdby++; break;   // read byte
        case 5: wr(r, 0x30); run_samples(r, rn(5)); nrb++; break;                     // read and branch
        case 6: if (rn(3) == 0) { wr(r, 0x70); run_samples(r, rn(5)); } break;       // reset
        case 7: wr(r, rn(2) ? 0x00 : 0x20 | rn(16)); break;                           // nop
        case 8: case 9: run_samples(r, 1 + rn(1500)); break;
        case 10: case 11: rd(r); nstatus++; break;
        case 12: {                                   // random byte write (any command, any state)
            uint8_t v = (uint8_t)rn(256);
            if ((v & 0x70) == 0x70 && rn(4)) v &= 0x6F; // keep resets rarer
            wr(r, v); run_samples(r, rn(400)); break; }
        case 13: load_addr(r, rnd() & 0x7FFF, 5); wr(r, 0x10); rd(r); rd(r); break;
        }
    }
    std::printf("PASS: %ld ops, %ld samples compared (speak:%ld spkext:%ld rdby:%ld rb:%ld status:%ld) - outputs and %d state fields identical\n",
                total_ops, r.samples, ntalk, nspkext, nrdby, nrb, nstatus, 52);
    std::printf("held-command waits %ld, fifo-full waits %ld; coverage: talking samples %ld (voiced %ld, unvoiced %ld, inhibit-frames %ld), non-idle samples %ld, max |sample| %ld\n", g_pending, g_fifofull, g_talk, g_voiced, g_unv, g_inh, g_nonzero, g_maxabs);
    return 0;
}
