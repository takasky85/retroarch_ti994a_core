LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE     := ti99_libretro
LOCAL_SRC_FILES  := ../src/cpu9900.c ../src/vdp9918.c ../src/psg76489.c ../src/speech.c ../src/osk.c ../src/picker.c \
                    ../src/ti99.c ../src/zipread.c ../src/loader.c \
                    ../src/libretro_core.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../src $(LOCAL_PATH)/../third_party/libretro
LOCAL_CFLAGS     := -std=gnu99 -O2 -fvisibility=hidden -Wall
# armeabi-v7a: plain ARMv7 code, no NEON, no JIT -- nothing in the core needs either.
LOCAL_ARM_MODE   := arm
# Android 15+ devices with 16 KB pages need 16 KB aligned segments (the 32-bit ABI does not).
ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
LOCAL_LDFLAGS    += -Wl,-z,max-page-size=16384
endif
include $(BUILD_SHARED_LIBRARY)
