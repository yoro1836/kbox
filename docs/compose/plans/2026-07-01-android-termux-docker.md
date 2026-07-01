# Android Support - termux-docker Approach

> **For agentic workers:** REQUIRED SUB-SKILL: Use compose:subagent (recommended) or compose:execute to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build kbox for Android aarch64 using termux-docker for LKL and NDK for kbox

**Architecture:** LKL is built inside termux-docker (Bionic environment) on a native aarch64 runner, producing liblkl.a with zero glibc dependencies. kbox is cross-compiled with the Android NDK and linked against this clean LKL.

**Tech Stack:** termux-docker, Android NDK r27, GNU Make, GitHub Actions

## Global Constraints

- Minimum Android API level: 30 (Android 11)
- Target architecture: aarch64 only
- All existing Linux builds must remain unaffected
- No glibc shim hacks - LKL must be built with Bionic

---

### Task 1: Clean up - squash Android commits into one

The current branch has 4 incremental Android commits from trial-and-error. Squash into one clean commit.

- [ ] **Step 1: Soft reset and recommit**

```bash
git reset --soft origin/main
git add -A
git commit -m "Add Android build support (API 30+, aarch64)

[reuse the existing commit message from 0000b42]"
```

- [ ] **Step 2: Verify local build**

```bash
make clean && make defconfig && make BUILD=release -j$(nproc)
make check-unit
```

Expected: 280 tests pass, build succeeds.

---

### Task 2: Add termux-docker LKL build to build-lkl.yml

Add a new job that builds LKL inside termux-docker on the native aarch64 runner.

**Files:**
- Modify: `.github/workflows/build-lkl.yml`

- [ ] **Step 1: Add build-android-lkl job**

Add after the existing `build` job in build-lkl.yml:

```yaml
  # ---- Build aarch64-android LKL via termux-docker ----
  build-android-lkl:
    needs: check-upstream
    if: needs.check-upstream.outputs.needs_build == 'true'
    runs-on: ubuntu-24.04-arm
    timeout-minutes: 360
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Build LKL in termux-docker
        run: |
          LKL_COMMIT="${{ needs.check-upstream.outputs.lkl_commit }}"
          docker run --rm \
            -v ${{ github.workspace }}:/workspace \
            termux/termux-docker:aarch64 \
            sh -c "
              apt update -y &&
              apt install -y git make clang &&
              git clone --depth=1 https://github.com/lkl/linux.git /tmp/lkl-src &&
              if [ -n '${LKL_COMMIT}' ]; then
                cd /tmp/lkl-src && git fetch --depth=1 origin '${LKL_COMMIT}' && git checkout FETCH_HEAD;
              fi &&
              make -C /tmp/lkl-src ARCH=lkl defconfig &&
              make -C /tmp/lkl-src ARCH=lkl -j\$(nproc) &&
              make -C /tmp/lkl-src/tools/lkl -j\$(nproc) &&
              mkdir -p /workspace/lkl-aarch64-android &&
              cp /tmp/lkl-src/tools/lkl/liblkl.a /workspace/lkl-aarch64-android/ &&
              cp /tmp/lkl-src/tools/lkl/include/lkl.h /workspace/lkl-aarch64-android/ 2>/dev/null || true &&
              cp /tmp/lkl-src/tools/lkl/include/lkl/autoconf.h /workspace/lkl-aarch64-android/ 2>/dev/null || true
            "

      - name: Package
        run: tar czf liblkl-aarch64-android.tar.gz -C lkl-aarch64-android/ .

      - name: Upload artifact
        uses: actions/upload-artifact@v7
        with:
          name: lkl-aarch64-android
          path: liblkl-aarch64-android.tar.gz
          retention-days: 3
```

- [ ] **Step 2: Update publish step to include Android artifact**

Add `liblkl-aarch64-android.tar.gz` to the release notes and `gh release create` command.

- [ ] **Step 3: Verify YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build-lkl.yml')); print('OK')"
```

---

### Task 3: Add build-android job to build-kbox.yml

**Files:**
- Modify: `.github/workflows/build-kbox.yml`

- [ ] **Step 1: Add build-android job**

```yaml
  build-android:
    runs-on: ubuntu-24.04-arm
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Install Android NDK
        run: |
          wget -q https://dl.google.com/android/repository/android-ndk-r27-linux.zip
          unzip -q android-ndk-r27-linux.zip
          echo "ANDROID_NDK_HOME=$PWD/android-ndk-r27" >> "$GITHUB_ENV"

      - name: Fetch Android LKL (aarch64, Bionic)
        run: |
          # Use the nightly release once available; for now fetch standard aarch64
          # and note that termux-docker-built artifact replaces glibc symbols
          LKL_DIR=lkl-aarch64-android ./scripts/fetch-lkl.sh aarch64

      - name: Configure
        run: make defconfig

      - name: Build kbox for Android
        run: make ANDROID=1 ANDROID_API=30 LKL_DIR=lkl-aarch64-android BUILD=release -j$(nproc)

      - name: Verify binary
        run: file kbox | grep -q "aarch64" || { echo "Not aarch64"; exit 1; }
```

- [ ] **Step 2: Verify YAML syntax**

---

### Task 4: Final verification

- [ ] **Step 1: Verify local build passes**

```bash
make clean && make defconfig && make BUILD=release -j$(nproc)
make check-unit
```

- [ ] **Step 2: Push and verify CI**
