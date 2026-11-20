# ti99_libretro.so for ARM Linux handhelds (modded Atari Gamestation Pro / Intellivision Sprint, ...)

**Status: best guess, not tested on any of these devices.** The core itself is plain C99 (no JIT, no assembly, no OS
calls other than `fopen`/`malloc`), so it only has to match the device's CPU ABI and C library.

## Which file
| Zip | Use it when |
|---|---|
| `...-linux-armv7hf.zip` | **try this first.** 32-bit ARM, hard-float (`arm-linux-gnueabihf`), VFPv3-D16 baseline (runs on Cortex-A7/A9 etc.). This is what the Rockchip-style Buildroot firmwares normally use. |
| `...-linux-armv7sf.zip` | the device's other libraries are soft-float (`/lib/ld-linux.so.3` instead of `/lib/ld-linux-armhf.so.3`) |
| `...-linux-aarch64.zip` | the device is 64-bit (`file /usr/bin/retroarch` says "ARM aarch64") |

Find out on the device: `uname -m` (armv7l = 32-bit, aarch64 = 64-bit) and `file /bin/busybox` (tells hard/soft float and the dynamic loader).
If the core fails to load with `GLIBC_x.y not found` or a missing `libc.so.6`, the firmware uses another libc (musl/uClibc);
tell me the output of `file /bin/busybox` and `ls /lib` and I will build against it.

## Where it goes (guess)
The community firmware for these devices loads RetroArch-style cores from `/usr/lib/libretro/` (or the `cores` folder of its RetroArch) with a
matching `.info` file in its `info` folder. Copy `ti99_libretro.so` there and `ti99_libretro.info` next to the other `.info` files.
Put `ti99_4a.zip` (and optionally `ti99_speech.zip`) from MAME in the system/BIOS folder the frontend uses.

Notes: the Sprint's 256 MB RAM / small flash are no problem (the core uses about 1 MB of RAM); the Gamestation Go's hardware is not documented, so no
build is specific to it. Controls: the handheld's pad becomes the TI joystick (B/A = fire); the on-screen keyboard (Select) is how you type.
