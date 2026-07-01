#!/bin/sh
# SPDX-License-Identifier: MIT
# Fetch prebuilt liblkl.a from the lkl-nightly release on sysprog21/kbox.
#
# Usage: ./scripts/fetch-lkl.sh [ARCH]
#   ARCH defaults to the host architecture (x86_64 or aarch64).
#   Override LKL_DIR to change output directory.
#
# Download order:
#   1. lkl-nightly release on sysprog21/kbox (curl, no auth)
#   2. GitHub CLI (gh) -- same release, authenticated
#   3. Manual instructions

set -eu

. "$(cd "$(dirname "$0")" && pwd)/common.sh"

detect_arch "${1:-}"

LKL_DIR="${LKL_DIR:-lkl-${ARCH}}"
REPO="${KBOX_REPO:-sysprog21/kbox}"
NIGHTLY_TAG="${KBOX_LKL_TAG:-lkl-nightly}"
ASSET="${ASSET:-liblkl-${ARCH}.tar.gz}"
SHA256_FILE="scripts/lkl-sha256.txt"

mkdir -p "$LKL_DIR"

# Already present?
if [ -f "${LKL_DIR}/liblkl.a" ]; then
    echo "OK: ${LKL_DIR}/liblkl.a (already exists)"
    exit 0
fi

# --- Method 1: GitHub Releases (curl, no auth) ---
try_release()
{
    if ! command -v curl > /dev/null 2>&1; then
        return 1
    fi

    URL="https://github.com/${REPO}/releases/download/${NIGHTLY_TAG}/${ASSET}"
    echo "Downloading ${URL}..."
    curl -fSL -o "${LKL_DIR}/${ASSET}" "$URL" || return 1

    verify_sha256 "${LKL_DIR}/${ASSET}" "$SHA256_FILE" "$ASSET"

    tar xzf "${LKL_DIR}/${ASSET}" -C "$LKL_DIR"
    rm -f "${LKL_DIR}/${ASSET}"
    return 0
}

# --- Method 2: gh CLI ---
try_gh()
{
    if ! command -v gh > /dev/null 2>&1; then
        return 1
    fi

    echo "Fetching ${ASSET} via gh CLI..."
    TMPDIR=$(mktemp -d)
    gh release download "$NIGHTLY_TAG" \
        --repo "$REPO" \
        --pattern "$ASSET" \
        --dir "$TMPDIR" 2> /dev/null || {
        rm -rf "$TMPDIR"
        return 1
    }

    verify_sha256 "${TMPDIR}/${ASSET}" "$SHA256_FILE" "$ASSET"

    tar xzf "${TMPDIR}/${ASSET}" -C "$LKL_DIR"
    rm -rf "$TMPDIR"
    return 0
}

# Try methods in order.
if try_release; then
    :
elif try_gh; then
    :
else
    cat >&2 << EOF
Cannot fetch liblkl.a automatically.

Manual download:
  https://github.com/${REPO}/releases/tag/${NIGHTLY_TAG}
  Download ${ASSET}, then: tar xzf ${ASSET} -C ${LKL_DIR}/

Or build from source:
  git clone https://github.com/lkl/linux.git
  cd linux && make ARCH=lkl defconfig && make ARCH=lkl -j\$(nproc)
  cp tools/lkl/liblkl.a ${LKL_DIR}/

EOF
    exit 1
fi

if [ -f "${LKL_DIR}/liblkl.a" ]; then
    echo "OK: ${LKL_DIR}/liblkl.a"
else
    die "Download succeeded but liblkl.a not found in ${LKL_DIR}/"
fi
