// Builds MAME's real, unmodified sources against the shim and exposes a C interface.
#define private public
#define protected public
#include "/home/claude/mamedev/mame_sparse/src/devices/sound/tms5220.cpp"
#include "/home/claude/mamedev/mame_sparse/src/devices/machine/tms6100.cpp"
#undef private
#undef protected
