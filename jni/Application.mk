# ndk-build:   ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
# 32-bit ARM is the primary target (Android TV boxes); the others cost nothing extra.
# CI builds one ABI at a time (APP_ABI=... on the command line).
APP_ABI      := armeabi-v7a arm64-v8a x86 x86_64
# android-21 is the lowest level current NDKs accept (android-16 no longer builds); the core only uses fopen/malloc,
# so keeping the level low keeps old Android TV boxes (Android 5+) working.
APP_PLATFORM := android-21
APP_OPTIM    := release
