# Android Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use compose:subagent (recommended) or compose:execute to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Android cross-compilation support to kbox (API 30+, aarch64, NDK toolchain)

**Architecture:** Conditional compilation via `#ifdef __ANDROID__` for Bionic-specific differences, NDK cross-compilation via `ANDROID_NDK_HOME` environment variable, LKL cross-build script for Bionic target.

**Tech Stack:** Android NDK r27+, GNU Make, C (gnu11), LKL

## Global Constraints

- Minimum Android API level: 30 (Android 11)
- Target architecture: aarch64 only
- All existing Linux builds must remain unaffected
- Commits must include `Change-Id` trailer (commit-msg hook)
- Subject lines: capitalized, 11-79 chars, no trailing period
- Vanity hash prefix `0000` required (run `scripts/vanity-hash.py`)

---

### Task 1: Add Android detection to build system

**Covers:** Build system Android support

**Files:**
- Modify: `mk/toolchain.mk:1-58`
- Create: `mk/android.mk`

**Interfaces:**
- Produces: `IS_ANDROID` make variable, NDK toolchain paths, conditional LDLIBS

- [ ] **Step 1: Create `mk/android.mk`**

```makefile
# mk/android.mk - Android NDK cross-compilation support
#
# Usage: make ANDROID=1 [ANDROID_API=30] [ANDROID_NDK_HOME=/path/to/ndk]
#
# When ANDROID=1, overrides CC/AR/STRIP with NDK toolchain binaries
# and sets appropriate flags for Bionic libc.

ifdef ANDROID

ANDROID_API    ?= 30
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
CFLAGS     += -DANDROID -D__ANDROID_API__=$(ANDROID_API)
# Bionic does not have separate -lrt
LDLIBS     := $(filter-out -lrt,$(LDLIBS))

endif
```

- [ ] **Step 2: Modify `mk/toolchain.mk` to include android.mk and handle `-lrt`**

In `mk/toolchain.mk`, add after line 1 (the comment):
```makefile
# Android NDK cross-compilation (must be included before other flags)
-include mk/android.mk
```

Change line 58 from:
```makefile
LDLIBS   = -llkl -lpthread -ldl -lm -lrt
```
to:
```makefile
LDLIBS   = -llkl -lpthread -ldl -lm
ifndef IS_ANDROID
LDLIBS  += -lrt
endif
```

- [ ] **Step 3: Verify native build still works**

Run: `make clean && make defconfig && make BUILD=release -j$(nproc)`
Expected: Build succeeds as before (IS_ANDROID not set, -lrt included)

- [ ] **Step 4: Commit**

```bash
git add mk/android.mk mk/toolchain.mk
git commit -m "Add Android NDK cross-compilation support to build system"
```

---

### Task 2: Replace fexecve with execveat for Android

**Covers:** Bionic libc compatibility

**Files:**
- Modify: `src/seccomp-supervisor.c:604-607`

**Interfaces:**
- Consumes: `exec_memfd` (int file descriptor), `argv` (char *const []), `environ` (char **)
- Produces: Same behavior via `execveat()` on Android

- [ ] **Step 1: Add `#include <sys/syscall.h>` if not present**

Check if `<sys/syscall.h>` is already included. If not, add it near the top includes.

- [ ] **Step 2: Replace fexecve call**

In `src/seccomp-supervisor.c`, change lines 604-605 from:
```c
            if (exec_memfd >= 0)
                fexecve(exec_memfd, (char *const *) argv, environ);
```
to:
```c
            if (exec_memfd >= 0) {
#ifdef __ANDROID__
                /* Bionic lacks fexecve(); use execveat via /proc/self/fd/N */
                char fdpath[64];
                snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", exec_memfd);
                execveat(AT_FDCWD, fdpath, (char *const *) argv, environ, 0);
#else
                fexecve(exec_memfd, (char *const *) argv, environ);
#endif
            }
```

- [ ] **Step 3: Verify native build**

Run: `make clean && make defconfig && make BUILD=release -j$(nproc)`
Expected: Build succeeds, `fexecve` path unchanged on Linux

- [ ] **Step 4: Commit**

```bash
git add src/seccomp-supervisor.c
git commit -m "Replace fexecve with execveat for Android Bionic compatibility"
```

---

### Task 3: Add Android LKL cross-build script

**Covers:** LKL Bionic cross-compilation

**Files:**
- Create: `scripts/build-lkl-android.sh`
- Modify: `mk/deps.mk:1-38`

**Interfaces:**
- Consumes: `ANDROID_NDK_HOME`, `ANDROID_API` env vars, LKL source tree
- Produces: `lkl-aarch64-android/liblkl.a` (Bionic-linked)

- [ ] **Step 1: Create `scripts/build-lkl-android.sh`**

```bash
#!/bin/sh
# SPDX-License-Identifier: MIT
# Build liblkl.a for Android (Bionic libc) using Android NDK.
#
# Usage: ./scripts/build-lkl-android.sh
#   Requires: ANDROID_NDK_HOME environment variable
#   Optional: ANDROID_API (default: 30), LKL_SRC, LKL_REF
#
# Output: lkl-aarch64-android/liblkl.a

set -eu

. "$(cd "$(dirname "$0")" && pwd)/common.sh"

ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-}"
ANDROID_API="${ANDROID_API:-30}"

[ -n "$ANDROID_NDK_HOME" ] || die "ANDROID_NDK_HOME is not set"

NDK_TOOLCHAIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64"
[ -d "$NDK_TOOLCHAIN" ] || die "NDK toolchain not found: ${NDK_TOOLCHAIN}"

NDK_TARGET="aarch64-linux-android${ANDROID_API}"
NDK_CC="${NDK_TOOLCHAIN}/bin/${NDK_TARGET}-clang"

[ -x "$NDK_CC" ] || die "NDK compiler not found: ${NDK_CC}"

LKL_DIR="${LKL_DIR:-lkl-aarch64-android}"
LKL_SRC="${LKL_SRC:-build/lkl-src}"
LKL_UPSTREAM="https://github.com/lkl/linux"

# ---- Clone or reuse source ----

if [ ! -d "${LKL_SRC}/.git" ]; then
    echo "  CLONE   ${LKL_UPSTREAM} -> ${LKL_SRC}"
    mkdir -p "$(dirname "${LKL_SRC}")"
    git clone --depth=1 "${LKL_UPSTREAM}" "${LKL_SRC}"
fi

if [ -n "${LKL_REF:-}" ]; then
    echo "  CHECKOUT ${LKL_REF}"
    git -C "${LKL_SRC}" fetch --depth=1 origin "${LKL_REF}"
    git -C "${LKL_SRC}" checkout FETCH_HEAD
fi

# ---- Configure ----

if [ ! -f "${LKL_SRC}/.config" ]; then
    echo "  CONFIG  ARCH=lkl defconfig (Android)"
    make -C "${LKL_SRC}" ARCH=lkl defconfig

    for opt in \
        CONFIG_DEVTMPFS \
        CONFIG_DEVTMPFS_MOUNT \
        CONFIG_DEVPTS_FS \
        CONFIG_PROC_SYSCTL \
        CONFIG_PRINTK; do
        "${LKL_SRC}/scripts/config" --file "${LKL_SRC}/.config" --enable "${opt}"
    done

    for opt in \
        CONFIG_MODULES \
        CONFIG_SOUND \
        CONFIG_USB_SUPPORT \
        CONFIG_INPUT \
        CONFIG_NFS_FS \
        CONFIG_CIFS; do
        "${LKL_SRC}/scripts/config" --file "${LKL_SRC}/.config" --disable "${opt}"
    done

    make -C "${LKL_SRC}" ARCH=lkl olddefconfig
fi

# ---- Build with NDK cross-compiler ----

NPROC=$(nproc 2>/dev/null || echo 1)

# LKL kernel build: use NDK clang as cross-compiler
# ARCH=lkl uses its own Kbuild, we override CROSS_COMPILE and CC
export CROSS_COMPILE="${NDK_TOOLCHAIN}/bin/${NDK_TARGET}-"
export CC="${NDK_CC}"

echo "  BUILD   ARCH=lkl kernel (Android, -j${NPROC})"
make -C "${LKL_SRC}" ARCH=lkl \
    CC="${NDK_CC}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    -j"${NPROC}"

echo "  BUILD   tools/lkl (Android, -j${NPROC})"
make -C "${LKL_SRC}/tools/lkl" \
    CC="${NDK_CC}" \
    -j"${NPROC}"

# ---- Verify ----

test -f "${LKL_SRC}/tools/lkl/liblkl.a" \
    || die "build succeeded but liblkl.a not found"

echo "  VERIFY  symbols"
for sym in lkl_init lkl_start_kernel lkl_cleanup lkl_syscall \
    lkl_strerror lkl_disk_add lkl_mount_dev \
    lkl_host_ops lkl_dev_blk_ops; do
    if ! nm "${LKL_SRC}/tools/lkl/liblkl.a" 2>/dev/null \
        | awk -v s="$sym" '$3==s && $2~/^[TtDdBbRr]$/{found=1} END{exit !found}'; then
        die "MISSING symbol: ${sym}"
    fi
done

# ---- Install ----

echo "  INSTALL ${LKL_DIR}/"
mkdir -p "${LKL_DIR}"

cp "${LKL_SRC}/tools/lkl/liblkl.a" "${LKL_DIR}/"
cp "${LKL_SRC}/tools/lkl/include/lkl.h" "${LKL_DIR}/" 2>/dev/null || true
cp "${LKL_SRC}/tools/lkl/include/lkl/autoconf.h" "${LKL_DIR}/" 2>/dev/null || true

printf 'commit=%s\ndate=%s\narch=%s\ntarget=android\napi=%s\n' \
    "$(git -C "${LKL_SRC}" rev-parse HEAD)" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    "aarch64" \
    "${ANDROID_API}" \
    > "${LKL_DIR}/BUILD_INFO"

(cd "${LKL_DIR}" && sha256sum ./* > sha256sums.txt 2>/dev/null || true)

echo "OK: ${LKL_DIR}/liblkl.a (Android API ${ANDROID_API})"
```

- [ ] **Step 2: Make script executable**

```bash
chmod +x scripts/build-lkl-android.sh
```

- [ ] **Step 3: Add Android build target to `mk/deps.mk`**

Add after the existing `build-lkl` target (after line 38):
```makefile
.PHONY: build-lkl-android
build-lkl-android:
	@echo "  BUILD   lkl (Android)"
	$(Q)./scripts/build-lkl-android.sh
```

- [ ] **Step 4: Verify script syntax**

```bash
bash -n scripts/build-lkl-android.sh
```
Expected: No output (syntax OK)

- [ ] **Step 5: Commit**

```bash
git add scripts/build-lkl-android.sh mk/deps.mk
git commit -m "Add Android LKL cross-build script for Bionic target"
```

---

### Task 4: Add Android CI jobs

**Covers:** CI/CD Android support

**Files:**
- Modify: `.github/workflows/build-lkl.yml`
- Modify: `.github/workflows/build-kbox.yml`

**Interfaces:**
- Produces: `liblkl-aarch64-android.tar.gz` artifact, `kbox-android` build artifact

- [ ] **Step 1: Add Android LKL build job to `build-lkl.yml`**

Add to the matrix in the `build` job (after the riscv64 entry, around line 109):
```yaml
          - arch: aarch64-android
            runner: ubuntu-24.04
```

Add a step before "Build LKL from source" to install NDK:
```yaml
      - name: Install Android NDK
        if: contains(matrix.arch, 'android')
        run: |
          wget -q https://dl.google.com/android/repository/android-ndk-r27-linux.zip
          unzip -q android-ndk-r27-linux.zip
          echo "ANDROID_NDK_HOME=$PWD/android-ndk-r27" >> "$GITHUB_ENV"
```

Change the build step to be conditional:
```yaml
      - name: Build LKL from source
        env:
          LKL_REF: ${{ needs.check-upstream.outputs.lkl_commit }}
        run: |
          if [[ "${{ matrix.arch }}" == *"android"* ]]; then
            make build-lkl-android
          else
            make build-lkl
          fi
```

Update the publish step to include Android artifact:
```yaml
          Contains: liblkl-x86_64.tar.gz, liblkl-aarch64.tar.gz, liblkl-riscv64.tar.gz, liblkl-aarch64-android.tar.gz
```

And add to the `gh release create` command:
```yaml
            dist/liblkl-aarch64-android.tar.gz
```

- [ ] **Step 2: Add Android kbox cross-build job to `build-kbox.yml`**

Add a new job after `build-kbox`:
```yaml
  # ---- Android cross-compilation verification ----
  build-android:
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Install Android NDK
        run: |
          wget -q https://dl.google.com/android/repository/android-ndk-r27-linux.zip
          unzip -q android-ndk-r27-linux.zip
          echo "ANDROID_NDK_HOME=$PWD/android-ndk-r27" >> "$GITHUB_ENV"

      - name: Fetch prebuilt LKL (Android)
        run: |
          LKL_DIR=lkl-aarch64-android ./scripts/fetch-lkl.sh aarch64

      - name: Configure
        run: make defconfig

      - name: Build kbox for Android
        run: make ANDROID=1 ANDROID_API=30 BUILD=release -j$(nproc)

      - name: Verify binary
        run: |
          file kbox | grep -q "aarch64" || { echo "Not an aarch64 binary"; exit 1; }
          file kbox | grep -q "dynamically linked" || { echo "Not dynamically linked"; exit 1; }
```

- [ ] **Step 3: Verify YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build-lkl.yml')); print('OK')"
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build-kbox.yml')); print('OK')"
```

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build-lkl.yml .github/workflows/build-kbox.yml
git commit -m "Add Android cross-compilation CI jobs"
```

---

### Task 5: Update fetch-lkl.sh for Android artifacts

**Covers:** LKL fetch support for Android

**Files:**
- Modify: `scripts/fetch-lkl.sh`

**Interfaces:**
- Consumes: `ANDROID_TARGET=1` env var to select Android artifact
- Produces: Downloads `liblkl-aarch64-android.tar.gz` when Android target requested

- [ ] **Step 1: Add Android artifact detection**

In `scripts/fetch-lkl.sh`, after line 23 (`ASSET="liblkl-${ARCH}.tar.gz"`), add:
```bash
# Android target: use Bionic-linked LKL
if [ "${ANDROID_TARGET:-}" = "1" ]; then
    ASSET="liblkl-aarch64-android.tar.gz"
    LKL_DIR="${LKL_DIR:-lkl-aarch64-android}"
fi
```

- [ ] **Step 2: Verify script syntax**

```bash
bash -n scripts/fetch-lkl.sh
```

- [ ] **Step 3: Commit**

```bash
git add scripts/fetch-lkl.sh
git commit -m "Support Android LKL artifact in fetch-lkl script"
```
