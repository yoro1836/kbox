# mk/android.mk - Android NDK cross-compilation support
#
# Usage: make ANDROID=1 [ANDROID_API=30] [ANDROID_NDK_HOME=/path/to/ndk]
#
# When ANDROID=1, overrides CC/AR/STRIP with NDK toolchain binaries
# and sets appropriate flags for Bionic libc.

ifdef ANDROID

ANDROID_API     ?= 30
ANDROID_NDK_HOME ?= $(ANDROID_NDK_HOME)

ifeq ($(strip $(ANDROID_NDK_HOME)),)
  $(error ANDROID_NDK_HOME is not set. Install Android NDK and set ANDROID_NDK_HOME)
endif

# NDK toolchain paths (r27+ layout)
NDK_TOOLCHAIN := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64
NDK_TARGET    := aarch64-linux-android$(ANDROID_API)

CC      := $(NDK_TOOLCHAIN)/bin/$(NDK_TARGET)-clang
CXX     := $(NDK_TOOLCHAIN)/bin/$(NDK_TARGET)-clang++
AR      := $(NDK_TOOLCHAIN)/bin/llvm-ar
STRIP   := $(NDK_TOOLCHAIN)/bin/llvm-strip

# Android-specific flags
IS_ANDROID := 1
CFLAGS     += -DANDROID -D__ANDROID_API__=$(ANDROID_API) -DNDEBUG
# Fully static linking: no dynamic dependencies at runtime.
# This also resolves glibc symbols at build time via NDK static libs.
LDFLAGS    += -static

endif
