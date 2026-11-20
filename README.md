# TI-99/4A core for RetroArch (work in progress, v0.3.0)

A from-scratch, portable **C99** libretro core for the TI-99/4A home computer.
It reads **MAME's file formats** (the `ti99_4a.zip` BIOS set and MAME's `ti99_cart` software-list zips),
but it is not MAME: it is a small dedicated emulator (about 3,000 lines) so that it can run on 32-bit
Android TV boxes where MAME cannot.

## Honest status

| | |
|---|---|
| Built & unit-tested on 64-bit Linux (gcc) | yes: 716 unit checks + 72 frontend-harness checks (CI also runs them as a 32-bit x86 build and under QEMU for ARM) |
| Runs in **RetroArch 1.22.2** on Windows 11 x64, an Android phone and a Google TV | reported by the user for v0.2.1: games run, the on-screen keyboard and speech work |
| Speech synthesizer | chip + voice-ROM logic checked **bit-for-bit against MAME's own `tms5220`/`tms6100` code** (8,000-op random streams, ~10^8 samples, plus 20 deliberate bugs injected: 19 caught, 1 equivalent). **Not heard with the real vocabulary ROM**: no `ti99_speech.zip` was available here, so the bus integration, CPU timing and mixing are tested with synthetic ROM data only. |
| **Windows x64 DLL, Android 32/64-bit ARM, Linux ARM** | built by GitHub Actions on every push and attached to each release (`.github/workflows/main.yml`). The Windows/Android ones are confirmed working by the user (v0.2.1); the **Linux ARM** ones (for the modded Atari Gamestation Pro / Intellivision Sprint) are a **best guess and untested on those devices** |
| Tested with a **real TI BIOS / real games** | **by the user, not here.** The authoring environment has no real BIOS or game ROMs (tests use a synthetic mini "console ROM"); the user has run real games on Windows, an Android phone and a Google TV (v0.2.1). The v0.3.0 cartridge detection was checked against the user's sample ROMs only as far as loading, bank mapping and boot code allow without a real BIOS - please report any that still fail. |

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

Supported cartridge layouts: standard, 12 KB/16 KB banked (e.g. Extended BASIC), banked ROMs of any power-of-two size up to 2 MB (8 KB banks: paged377/378/379i, and the ROM half of `gromemu` carts with their GROM), MiniMem (4 KB RAM, exposed as save RAM), MBX (experimental).

### Raw / unlisted ROM images and the cartridge-type picker
A ROM that is not in the software list is detected from its **size, its `>AA` header at >6000 and its file names**:

| Image | Mapped as |
|---|---|
| up to 8 KB (or a `c`+`d` pair, e.g. `...c.bin` + `...d.bin`) | standard / 2 x 8 KB banks |
| 8 KB - 16 KB (e.g. a 12,162-byte homebrew) | 2 x 8 KB banks, the short last bank is padded |
| larger than 16 KB (32/64/128 KB ... homebrew, names like `...8.bin`) | banked ROM, 8 KB banks, normal order (writing to >6000 + 2 x bank selects it) |
| RPK / softlist `gromemu` | banked ROM + the cartridge GROM |
| softlist types the core does not emulate (`super`, `paged7`) | banked ROM (approximation) |

Text files, pictures and the like inside a zip are ignored, so a readme next to the ROM no longer ends up in the cartridge. Role letters at the end of a file name (`c`/`d`/`g`) only count with a digit in front of them or next to sibling files, so a single ROM called `sword.bin` stays one ROM.

When the layout is a **guess** the core says so on screen. When it is **really unsure** (no `>AA` header, or exactly 12 KB which could be Extended-BASIC style) it **stops before starting and shows a menu** of cartridge types (D-pad + **B** to choose, **A** to keep the detected one). You can also open the same menu at any time: **F10**, or **Select** (on-screen keyboard) -> **CART** key. Choosing restarts the machine with the new mapping. If you always want a particular type for a game, set the core option *Cartridge type* (use RetroArch's per-game core options). The choices:

* Automatic (detected) - Standard 8K ROM + GROMs - 2 x 8K banks (16K) - Banked ROM, 8K banks - Banked ROM, inverted order - Extended BASIC 12K - MBX - Mini Memory

Bank order check: for Mario Bros (64 KB, `marioE8.bin`), TI Invaders (16 KB) and the Sword homebrew (12 KB) the *normal* order is the one that works (verified by running Mario Bros' boot code in the core and by following the bank-switch code in the images); *inverted order* is there for images that need it.

## Controls

* **Keyboard** (RetroArch must be in "game focus" mode - Scroll Lock by default): PC keys map to the TI keyboard. `Alt` = FCTN, `Shift` and `Ctrl` as expected, Caps Lock toggles the alpha lock. Punctuation that the TI types with FCTN/SHIFT combos is translated (`'`, `[`, `]`, `\`, `` ` ``, `?`, `-` ...). Arrow keys / Backspace / Delete / Insert / Esc = FCTN+S/D/E/X, FCTN+S, FCTN+1, FCTN+2, FCTN+9.
* **RetroPad -> joystick** (ports 1 and 2): D-pad or left stick = joystick, **B / A = fire**. Controllers need no setup: RetroArch's normal controller mapping applies, and the buttons are listed under Controls in the RetroArch menu.
* **On-screen keyboard** (pad 1): press **Select** (or **F11** on a real keyboard) to pop it up over the bottom of the picture - the whole TI keyboard, plus FCTN shortcuts (QUIT, BACK, CLR, DEL, INS and the arrows). While it is open: D-pad / left stick moves the cursor (it repeats when held), **B** presses the highlighted key, **A** or Select closes it, **Y** moves it between bottom and top of the screen, **Start** still types ENTER. SHIFT, CTRL and FCTN are sticky: press once = applies to the next key (blue), twice = locked (green), a third time releases. CAPS toggles the alpha lock. Pad 1 does not reach the game while the keyboard is open (so the cursor does not move your player); pad 2 keeps working. Closing it releases every key.
* **Cartridge-type menu**: F10, or on-screen keyboard -> CART (see above). D-pad up/down, **B** / Start / Enter = choose, **A** / Select / Esc = cancel.
* **Convenience keys on pad 1** (so an Android TV remote/gamepad is enough to start most games): Start = ENTER, Y = SPACE, L/R/L2/R2 = 1/2/3/4, X = Y, L3 = FCTN+= (QUIT), R3 = FCTN+9 (BACK). (Select used to type SPACE in v0.1.0; it opens the keyboard now and SPACE moved to Y.)

## Core options
* *32K RAM expansion* (default on; applies on reset).
* *Sprites per scanline*: 4 (hardware, flicker) or 32 (no flicker).
* *Speech synthesizer* (default enabled; only has an effect when `ti99_speech.zip` is present; applies immediately).
* *Cartridge type* (default automatic; applies on restart) and *Ask for the cartridge type when unsure* (default on).

## Speech
The synthesizer is the CD2501E (TMS5220 family) with the TMS6100 voice ROM of the TI speech module, at >9000 (read) / >9400 (write). Games and cartridges that use it (for example Parsec and Terminal Emulator II; check each title) will speak through the normal audio output, mixed with the sound chip. Timing model: each access costs the CPU 75 clocks and the CPU is held while the chip's 16-byte buffer is full, as on the real module. The chip runs at the true 8 kHz sample rate and is resampled to 48 kHz. Savestates include it.

## Builds from GitHub (no local toolchain needed)
`.github/workflows/main.yml` builds on every push / pull request and, for a tag `v*` (or "Run workflow" with a release tag), attaches these zips to the GitHub release. Each zip holds the core and `ti99_libretro.info`:

| File | For |
|---|---|
| `ti99_libretro-windows-amd64.zip` | Windows 64-bit RetroArch (`cores\` + `info\`) |
| `ti99_libretro-android-armeabi-v7a.zip` | 32-bit Android / Google TV (`ti99_libretro_android.so`) |
| `ti99_libretro-android-arm64-v8a.zip` | 64-bit Android |
| `ti99_libretro-linux-amd64.zip` | desktop Linux |
| `ti99_libretro-linux-armv7hf.zip`, `-armv7sf.zip`, `-aarch64.zip` | **best guesses** for the modded Atari Gamestation Pro / Intellivision Sprint and other ARM Linux boxes - see `packaging/ARM-LINUX.md` |

## 32-bit Android (manual build)
```
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk APP_ABI=armeabi-v7a
```
builds `armeabi-v7a` (also arm64-v8a, x86, x86_64). Emulation cost on the desktop is ~0.5% of one core (~190x real time, the speech chip adds well under 0.01%), so even a slow Cortex-A7/A53 box should have a large margin (not measured on ARM).

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
src/picker.[ch]      cartridge-type menu (same drawing helpers as osk.c)
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
