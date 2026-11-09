#!/usr/bin/env bash
# Builds the arm64 release APK and signs it.
#
# What it needs:
#   * the Android SDK and NDK (ANDROID_HOME, ANDROID_NDK_HOME), JDK 17, CMake,
#     Ninja;
#   * ../generated - the translated game code, produced from the disc, not in
#     this repository.
#
# SDL3 comes from Aurora, which vendors it on a cross build; point
# MH3_SDL3_PREFIX at an installed SDL3 to link that instead.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/var/mnt/juegos_sdb2/MHFU/out/android-sdk}}"
ANDROID_NDK_ROOT="${ANDROID_NDK_HOME:-${ANDROID_SDK_ROOT}/ndk/${ANDROID_NDK_VERSION:-28.2.13676358}}"

BUILD_DIR="${ROOT_DIR}/build-android"

NINJA_BIN="$(command -v ninja || echo /usr/bin/ninja)"

# An installed SDL3 package, if one is given: otherwise Aurora builds its own.
SDL3_ARGS=()
if [ -n "${MH3_SDL3_PREFIX:-}" ]; then
  SDL3_ARGS=(
    -DAURORA_SDL3_PROVIDER=system
    -DCMAKE_FIND_ROOT_PATH="${MH3_SDL3_PREFIX}"
    -DSDL3_DIR="${MH3_SDL3_PREFIX}/lib/cmake/SDL3")
else
  SDL3_ARGS=(-DAURORA_SDL3_PROVIDER=vendor)
fi

echo "=== 1. Configuring CMake for Android arm64-v8a ==="
cmake -S "${ROOT_DIR}/runtime" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DMH3_AURORA_ROOT="${ROOT_DIR}/aurora-main" \
  -DAURORA_SDL3_LINKAGE=shared \
  "${SDL3_ARGS[@]}"

echo "=== 2. Building native libraries ==="
cmake --build "${BUILD_DIR}" --target libmain.so libpng16.so --parallel "$(nproc)"

echo "=== 3. Packaging and signing APK ==="
bash "${ROOT_DIR}/packaging/android/build_apk.sh"
