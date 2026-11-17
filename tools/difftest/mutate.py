import subprocess, sys, shutil, os
src = open('/home/claude/ti99core/src/speech.c').read()
muts = [
 ("lattice x-loop start", "for (i = 9; i >= 1; i--) s->x[i]", "for (i = 8; i >= 1; i--) s->x[i]"),
 ("interp shift index", "sp_interp_shift[s->ip]", "sp_interp_shift[(s->ip + 1) & 7]"),
 ("VSM bit order", "(s->vsm_sa >> 1) | ((s->vsm_sa << 7) & 0x80u)", "(s->vsm_sa << 1) | ((s->vsm_sa >> 7) & 1u)"),
 ("RB endianness", "rb = rom[s->vsm_addr & mask];\n            s->vsm_addr++;\n            rb |= (uint32_t)rom[s->vsm_addr & mask] << 8;", "rb = (uint32_t)rom[s->vsm_addr & mask] << 8;\n            s->vsm_addr++;\n            rb |= rom[s->vsm_addr & mask];"),
 ("FIFO bit order", "(s->fifo[s->fifo_head] >> s->fifo_bits_taken) & 1u", "(s->fifo[s->fifo_head] >> (7 - s->fifo_bits_taken)) & 1u"),
 ("K index", "sp_k_tab[idx][s->new_k[idx]]", "sp_k_tab[idx][s->new_k[idx] ^ 1]"),
 ("inhibit clause removed", "(s->olde && !new_sil) || (s->oldp && new_sil))", "(s->oldp && new_sil))"),
 ("chirp hold index", "s->pitch_count >= 51 ? 51 : s->pitch_count", "s->pitch_count >= 50 ? 50 : s->pitch_count"),
 ("clip_analog mask", "v &= ~0xF;", "v &= ~0x7;"),
 ("BL threshold", "(s->fifo_count <= 8) ? 1 : 0", "(s->fifo_count <= 7) ? 1 : 0"),
 ("LA nibble shift", "unsigned shift = 4u * (s->vsm_count + 1u);", "unsigned shift = 4u * (s->vsm_count + 2u);"),
 ("reset subcycle", "s->subcycle = 0;                                /* as MAME", "s->subcycle = 1;                                /* as MAME"),
 ("LFSR repeat count", "for (i = 0; i < 20; i++) {              /* the LFSR", "for (i = 0; i < 19; i++) {              /* the LFSR"),
 ("matrix multiply shift", "return asr(a * b, 9);", "return asr(a * b, 8);"),
 ("unvoiced excitation", "(s->rng & 1u) ? -64 : 0x40", "(s->rng & 1u) ? -63 : 0x40"),
 ("zpar vs uv_zpar", "int z = (idx < 4) ? s->zpar : s->uv_zpar;", "int z = (idx < 4) ? s->zpar : s->zpar;"),
 ("energy interp uses pitch zpar", "(s->cur_energy +\n                    asr(((int)sp_energy_tab[s->new_e] - s->cur_energy) * (1 - inh), shift)) * (1 - s->zpar)", "(s->cur_energy +\n                    asr(((int)sp_energy_tab[s->new_e] - s->cur_energy) * (1 - inh), shift)) * (1 - s->uv_zpar)"),
 ("status bit", "(s->buffer_empty ? 0x20u : 0u)", "(s->buffer_empty ? 0x10u : 0u)"),
 ("dummy read after LA", "vsm_cmd(s, VSM_LA, 0);\n        vsm_cmd(s, VSM_TB, 0);", "vsm_cmd(s, VSM_TB, 0);"),
 ("fifo start threshold", "if (!s->spen && old_low && !s->buffer_low)", "if (!s->spen && old_low && !s->buffer_low && s->fifo_count > 9)"),
 ("asr negative shift", "return v >= 0 ? (v >> n) : ~((~v) >> n);", "return v / (1 << n);"),
]
survived = []
for name, a, b in muts:
    if a not in src:
        print("!! mutation pattern not found:", name); survived.append(name + " (pattern missing)"); continue
    open('mut/speech_mut.c','w').write(src.replace(a, b, 1))
    r = subprocess.run("gcc -std=gnu99 -O1 -c mut/speech_mut.c -o mut/speech_mut.o -I/home/claude/ti99core/src && g++ -std=c++17 -O1 -Ishim -I/home/claude/mamedev/mame_sparse/src/devices difftest.cpp mut/speech_mut.o -o mut/difftest_mut 2>&1 && timeout 120 ./mut/difftest_mut 1500 7", shell=True, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    caught = any(l.startswith("MISMATCH") or "unexpected stall" in l for l in out) or r.returncode != 0
    first = next((l for l in out if l.startswith("MISMATCH") or "error" in l), out[-1] if out else "")
    print(("CAUGHT   " if caught else "SURVIVED ") + name + "  ->  " + first[:100])
    if not caught: survived.append(name)
print("\nsurvivors:", survived or "none")
