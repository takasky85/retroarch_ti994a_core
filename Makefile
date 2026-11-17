# TI-99/4A libretro core -- GNU make.
#   Windows (MSYS2 MinGW-w64 shell):   make            -> ti99_libretro.dll
#   Linux / macOS:                     make            -> ti99_libretro.so / .dylib
#   Tests:                             make test
#   Android: use ndk-build (see jni/), do not use this makefile.

CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu99 -Wall -Wextra -fvisibility=hidden -Ithird_party/libretro -Isrc
SRC      = src/cpu9900.c src/vdp9918.c src/psg76489.c src/speech.c src/osk.c src/ti99.c src/zipread.c src/loader.c src/libretro_core.c
OBJ      = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
  TARGET  = ti99_libretro.dll
  LDFLAGS += -shared -static-libgcc -Wl,--no-undefined
else ifeq ($(shell uname -s),Darwin)
  TARGET  = ti99_libretro.dylib
  LDFLAGS += -dynamiclib
  CFLAGS  += -fPIC
else
  TARGET  = ti99_libretro.so
  LDFLAGS += -shared
  CFLAGS  += -fPIC
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

TESTSRC = tests/test_main.c src/cpu9900.c src/vdp9918.c src/psg76489.c src/speech.c src/osk.c src/ti99.c src/zipread.c src/loader.c

test_ti99: $(TESTSRC)
	$(CC) -std=gnu99 -g -O1 -Wall -Wextra -Isrc -Ithird_party/libretro $(TESTSRC) -lm -o $@

# tests/make_testzip.py creates a deflate-compressed zip for the inflate tests
test: test_ti99
	python3 tests/make_testzip.py tests/deflate_test.zip
	./test_ti99 tests/deflate_test.zip

# Drives the built core through a fake libretro frontend (Linux/macOS only; uses dlopen).
hosttest: $(TARGET) tests/host_test.c
	$(CC) -std=gnu99 -g -O1 -Wall -Wextra -Isrc -Ithird_party/libretro tests/host_test.c src/zipread.c -ldl -o host_test
	./host_test ./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) test_ti99 host_test tests/deflate_test.zip

.PHONY: all test hosttest clean
