#!/bin/sh
# SPDX-License-Identifier: MIT
# Build liblkl.a for Android (Bionic libc) using Android NDK.
#
# NOTE: Must be run on an aarch64 host (native or CI runner).
#       Cross-compilation from x86_64 is not supported because
#       ARCH=lkl always targets the host architecture.
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

# Detect NDK prebuilt directory (varies by host architecture)
NDK_HOST=$(uname -m)
case "$NDK_HOST" in
    x86_64)  NDK_PREBUILT="linux-x86_64" ;;
    aarch64) NDK_PREBUILT="linux-aarch64" ;;
    *)       die "unsupported host architecture: $NDK_HOST" ;;
esac
NDK_TOOLCHAIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${NDK_PREBUILT}"
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

NDK_BIN="${NDK_TOOLCHAIN}/bin"
NDK_SYSROOT="${NDK_TOOLCHAIN}/sysroot"

# NDK uses LLVM tools, not GNU binutils. Create symlinks with the
# CROSS_COMPILE prefix so the kernel build system can find them.
SYMLINK_DIR=$(mktemp -d)
for tool in ar nm ld objcopy objdump ranlib strip; do
    ln -sf "${NDK_BIN}/llvm-${tool}" "${SYMLINK_DIR}/${NDK_TARGET}-${tool}" 2>/dev/null || true
done
# ld needs special handling - use lld
ln -sf "${NDK_BIN}/ld.lld" "${SYMLINK_DIR}/${NDK_TARGET}-ld"

CROSS_PREFIX="${SYMLINK_DIR}/${NDK_TARGET}-"

# Pass --sysroot via CC, not CLANG_FLAGS, so Makefile.clang can
# still append -fintegrated-as and other required flags.
CC_WITH_SYSROOT="${NDK_CC} --sysroot=${NDK_SYSROOT}"

# Wrap ld.lld with --target flag so the kernel link step uses aarch64
LD_WRAPPED="${SYMLINK_DIR}/ld-wrapped"
cat > "${LD_WRAPPED}" <<EOF
#!/bin/sh
exec "${NDK_BIN}/ld.lld" --target=aarch64-linux-gnu "\$@"
EOF
chmod +x "${LD_WRAPPED}"

echo "  BUILD   ARCH=lkl kernel (Android, -j${NPROC})"
make -C "${LKL_SRC}" ARCH=lkl \
    CC="${CC_WITH_SYSROOT}" \
    LD="${LD_WRAPPED}" \
    CROSS_COMPILE="${CROSS_PREFIX}" \
    CLANG_TARGET_FLAGS="aarch64-linux-gnu" \
    LLVM=1 \
    -j"${NPROC}"

echo "  BUILD   tools/lkl (Android, -j${NPROC})"
make -C "${LKL_SRC}/tools/lkl" \
    CC="${CC_WITH_SYSROOT}" \
    AR="${NDK_BIN}/llvm-ar" \
    NM="${NDK_BIN}/llvm-nm" \
    -j"${NPROC}"

rm -rf "${SYMLINK_DIR}"

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
