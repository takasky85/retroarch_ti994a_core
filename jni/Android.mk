LOCAL_PATH := $(call my-dir)
ROOT       := $(LOCAL_PATH)/..

include $(CLEAR_VARS)
LOCAL_MODULE     := ti99_libretro
LOCAL_SRC_FILES  := $(ROOT)/src/cpu9900.c $(ROOT)/src/vdp9918.c $(ROOT)/src/psg76489.c $(ROOT)/src/speech.c $(ROOT)/src/osk.c \
                    $(ROOT)/src/ti99.c $(ROOT)/src/zipread.c $(ROOT)/src/loader.c \
                    $(ROOT)/src/libretro_core.c
LOCAL_C_INCLUDES := $(ROOT)/src $(ROOT)/third_party/libretro
LOCAL_CFLAGS     := -std=gnu99 -O2 -fvisibility=hidden -Wall
# armeabi-v7a: plain ARMv7 code, no NEON, no JIT -- nothing in the core needs either.
LOCAL_ARM_MODE   := arm
include $(BUILD_SHARED_LIBRARY)
