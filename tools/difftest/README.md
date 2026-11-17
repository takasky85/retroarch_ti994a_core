# Speech differential test (needs a MAME source checkout)

`difftest.cpp` runs this core's `src/speech.c` in lockstep with MAME's real `tms5220.cpp` (and
`tms6100.cpp` for the voice ROM) on random command/data streams and compares every output sample and
about 50 internal state fields. `mutate.py` injects 20 deliberate bugs into `speech.c` and checks that
the test catches them (19/20 caught; the survivor is an equivalent mutant: chirp[50] == chirp[51] == 0).

```
# MAME = path of a MAME checkout (only src/devices/{sound,machine} is needed)
gcc -std=gnu99 -O1 -g -c ../../src/speech.c -o speech_c.o -I../../src
g++ -std=c++17 -O1 -g -Ishim -I$MAME/src/devices difftest.cpp speech_c.o -o difftest
./difftest 8000 1        # ops, seed
```
`mame_refs.cpp` is the glue that instantiates MAME's classes against the small stand-in headers in
`shim/`; the paths at the top of `mutate.py` assume the author's directory layout and need editing.
The MAME sources are BSD-3-Clause (see ../../third_party/MAME-NOTICE.txt) and are not included.
