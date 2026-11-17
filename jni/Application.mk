# ndk-build:   ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
# 32-bit ARM is the primary target (Android TV boxes); the others cost nothing extra.
APP_ABI      := armeabi-v7a arm64-v8a x86 x86_64
APP_PLATFORM := android-16
APP_OPTIM    := release
