# TI-99/4A core for RetroArch (work in progress, v0.2.1)

A from-scratch, portable **C99** libretro core for the TI-99/4A home computer.
It reads **MAME's file formats** (the `ti99_4a.zip` BIOS set and MAME's `ti99_cart` software-list zips),
but it is not MAME: it is a small dedicated emulator (about 3,000 lines) so that it can run on 32-bit
Android TV boxes where MAME cannot.

## Honest status

| | |
|---|---|
| Built & unit-tested on 64-bit Linux (gcc) | yes: 607 unit checks + 57 frontend-harness checks, also clean under AddressSanitizer/UBSan and `-Wall -Wextra -pedantic` |
| Boots to the BASIC prompt on **Windows 11 x64 in RetroArch** | reported by the user for v0.1.0 (the only evidence of that; not reproduced here) |
| Speech synthesizer | chip + voice-ROM logic checked **bit-for-bit against MAME's own `tms5220`/`tms6100` code** (8,000-op random streams, ~10^8 samples, plus 20 deliberate bugs injected: 19 caught, 1 equivalent). **Not heard with the real vocabulary ROM**: no `ti99_speech.zip` was available here, so the bus integration, CPU timing and mixing are tested with synthetic ROM data only. |
| **Windows x64 DLL** | **not built here** (no Windows toolchain in the authoring environment). Build it with the steps below. |
| **32-bit ARM / Android build** | **not built here** (no NDK). Files are included (`jni/`). Code uses fixed-width types only, no JIT, no assembly, no OS APIs besides `fopen`. |
| Tested with a **real TI BIOS / real games** | **no.** No ROMs were available when this was written. Tests use a synthetic mini "console ROM" assembled in the test code. Expect to find bugs the moment real software runs. Please report/bring me the first failures. |

## Build (Windows 11, 64-bit)

**Option A - MSYS2 MinGW-w64** (simplest): install MSYS2, open the *MINGW64* shell, then
```
pacman -S --needed mingw-w64-x86_64-gcc make
make            # -> ti99_libretro.dll
make test       # optional: unit tests
```
**Option B - Visual Studio 2019+ / CMake**
```
cmake -S . -B build -A x64
cmake --build build --config Release       # -> build/Release/ti99_libretro.dll
```
Copy `ti99_libretro.dll` to RetroArch's `cores\` folder and `ti99_libretro.info` to `info\`.

## Files you must supply

* **`system\ti99_4a.zip`** - MAME's TI-99/4A BIOS set (3 GROMs + the two 4 KB CPU ROM halves). Also looked for in `system\mame\`, `system\ti99\`, `system\mame\roms\` and next to the game.
* **`system\ti99_speech.zip`** (optional) - MAME's speech synthesizer ROM set (`cd2325a.u2a` + `cd2326a.u2b`, 2 x 16 KB). When it is found the speech synthesizer is **on by default**; without it the port simply is not there (as on a console without the speech module). Same search folders as the BIOS.
* Cartridge layouts need no extra file: the core has **MAME's `ti99_cart` software list built in** (319 cartridges; ROM/GROM files, offsets and bank-switching type). A newer `system\mame\hash\ti99_cart.xml` (or `system\hash\`, `system\ti99\hash\`) is used first if you have one. For zips that are in neither, the core guesses from the MAME/TI file names (`phm####c.bin` = ROM at >6000, `...d.bin` = second ROM bank, `phm####g3.bin` ... `g6.bin` = GROM at >6000 / >8000 / >A000 / >C000) and shows a notice.

## Content
The core asks RetroArch for the **file path only** (`need_fullpath`, `block_extract`, like the arcade cores) and reads the zip itself. v0.2.0 asked RetroArch to read the file into memory, and RetroArch 1.22.2 refused with "Could not read content file" before the core was called; if you see that message, you are running a build older than 0.2.1.

`.zip` (MAME software-list cartridges), `.rpk` (layout.xml packages), or raw `.bin/.c/.d/.g` dumps.
Content may be omitted ("Start Core") to boot to the TI title screen / BASIC.

Supported cartridge layouts: standard, 12 KB/16 KB banked (e.g. Extended BASIC), paged377/378/379i, MiniMem (4 KB RAM, exposed as save RAM), MBX (experimental).

## Controls

* **Keyboard** (RetroArch must be in "game focus" mode - Scroll Lock by default): PC keys map to the TI keyboard. `Alt` = FCTN, `Shift` and `Ctrl` as expected, Caps Lock toggles the alpha lock. Punctuation that the TI types with FCTN/SHIFT combos is translated (`'`, `[`, `]`, `\`, `` ` ``, `?`, `-` ...). Arrow keys / Backspace / Delete / Insert / Esc = FCTN+S/D/E/X, FCTN+S, FCTN+1, FCTN+2, FCTN+9.
* **RetroPad -> joystick** (ports 1 and 2): D-pad or left stick = joystick, **B / A = fire**. Controllers need no setup: RetroArch's normal controller mapping applies, and the buttons are listed under Controls in the RetroArch menu.
* **On-screen keyboard** (pad 1): press **Select** (or **F11** on a real keyboard) to pop it up over the bottom of the picture - the whole TI keyboard, plus FCTN shortcuts (QUIT, BACK, CLR, DEL, INS and the arrows). While it is open: D-pad / left stick moves the cursor (it repeats when held), **B** presses the highlighted key, **A** or Select closes it, **Y** moves it between bottom and top of the screen, **Start** still types ENTER. SHIFT, CTRL and FCTN are sticky: press once = applies to the next key (blue), twice = locked (green), a third time releases. CAPS toggles the alpha lock. Pad 1 does not reach the game while the keyboard is open (so the cursor does not move your player); pad 2 keeps working. Closing it releases every key.
* **Convenience keys on pad 1** (so an Android TV remote/gamepad is enough to start most games): Start = ENTER, Y = SPACE, L/R/L2/R2 = 1/2/3/4, X = Y, L3 = FCTN+= (QUIT), R3 = FCTN+9 (BACK). (Select used to type SPACE in v0.1.0; it opens the keyboard now and SPACE moved to Y.)

## Core options
* *32K RAM expansion* (default on; applies on reset).
* *Sprites per scanline*: 4 (hardware, flicker) or 32 (no flicker).
* *Speech synthesizer* (default enabled; only has an effect when `ti99_speech.zip` is present; applies immediately).

## Speech
The synthesizer is the CD2501E (TMS5220 family) with the TMS6100 voice ROM of the TI speech module, at >9000 (read) / >9400 (write). Games and cartridges that use it (for example Parsec and Terminal Emulator II; check each title) will speak through the normal audio output, mixed with the sound chip. Timing model: each access costs the CPU 75 clocks and the CPU is held while the chip's 16-byte buffer is full, as on the real module. The chip runs at the true 8 kHz sample rate and is resampled to 48 kHz. Savestates include it.

## 32-bit Android (later step)
```
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
```
builds `armeabi-v7a` (plus arm64-v8a, x86, x86_64). Emulation cost on the desktop is ~0.5% of one core (~190x real time, the speech chip adds well under 0.01%), so even a slow Cortex-A7/A53 box should have a large margin (not measured on ARM).

## What is NOT emulated yet
Disk controller / PEB cards, cassette, GRAM Kracker & other "super" cartridges, TI-99/4 (non-A) variants, RS232, mouse. Open-bus values, GROM edge cases, MBX banking and exact instruction timings are approximations (see comments in `src/ti99.c` and `src/cpu9900.c`). Savestates are tied to this core version and are not portable to big-endian hosts.

## Layout
```
src/cpu9900.[ch]     TMS9900 CPU (full instruction set, level-1 interrupt, wait states)
src/vdp9918.[ch]     TMS9918A video (all 4 modes, sprites, collision, 5th-sprite)
src/psg76489.[ch]    SN76489 sound
src/speech.[ch]      TMS5200/CD2501E speech chip + TMS6100 voice ROM (port of MAME's logic, see third_party/MAME-NOTICE.txt)
src/speech_tables.h  generated by tools/gen_speech_tables.py from MAME's tms5110r.hxx - do not edit
src/osk.[ch]         on-screen keyboard (logic + software rendering, no libretro dependency)
src/keymatrix.h      TI keyboard matrix codes shared by osk.c and libretro_core.c
src/softlist_data.h  generated by tools/gen_softlist.py: MAME's ti99_cart software list (CC0), trimmed
src/ti99.[ch]        console: memory map, GROM, TMS9901 (keyboard/timer/interrupts), cartridges, savestates
src/zipread.[ch]     zip reader + built-in inflate (no zlib dependency)
src/loader.[ch]      MAME BIOS / softlist / RPK loading
src/libretro_core.c  libretro API, input mapping, options
third_party/libretro/libretro.h   minimal stand-in; replace with the official header before submitting to the buildbot
tests/               unit tests (make test) and a fake-frontend harness (make hosttest)
tools/               table/list generators; tools/difftest = lockstep test against MAME's speech code (needs a MAME checkout)
third_party/MAME-NOTICE.txt   licences for the MAME-derived speech code and software list
```
