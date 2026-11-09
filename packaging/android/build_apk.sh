#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
# The SDK: ANDROID_HOME, then a project-local install (toolchain/), then the
# conventional location. Never a machine-specific path: CI and a laptop then
# take the same route.
ANDROID_SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
for candidate in "${PROJECT_ROOT:-${ROOT_DIR:-.}}/toolchain/android-sdk" "${HOME}/Android/Sdk"; do
    if [ -z "${ANDROID_SDK_ROOT}" ] && [ -d "${candidate}" ]; then
        ANDROID_SDK_ROOT="${candidate}"
    fi
done
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${HOME}/Android/Sdk}"
SDK_BUILD_TOOLS="${ANDROID_SDK_ROOT}/build-tools/${ANDROID_BUILD_TOOLS:-35.0.0}"
ANDROID_JAR="${ANDROID_SDK_ROOT}/platforms/android-${ANDROID_PLATFORM:-35}/android.jar"
ANDROID_NDK_ROOT="${ANDROID_NDK_HOME:-${ANDROID_SDK_ROOT}/ndk/${ANDROID_NDK_VERSION:-28.2.13676358}}"

# The NDK's prebuilt host tools: linux-x86_64 locally, but the path carries the
# host triple, so it is discovered rather than assumed.
NDK_STRIP=""
for candidate in "${ANDROID_NDK_ROOT}"/toolchains/llvm/prebuilt/*/bin/llvm-strip; do
    if [ -x "${candidate}" ]; then
        NDK_STRIP="${candidate}"
        break
    fi
done
if [ -z "${NDK_STRIP}" ]; then
    echo "build_apk.sh: no llvm-strip under ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt" >&2
    exit 1
fi

# The native libraries to package. libmain.so is this build's; SDL3 and libpng
# come from the same CMake build, because Aurora vendors both on a cross build -
# their exact paths are FetchContent's, so they are searched for rather than
# assumed. Each has an override for a layout that puts them elsewhere.
BUILD_DIR_NATIVE="${PROJECT_ROOT}/build-android"

find_lib() {
    # The first of the given globs that matches, newest file first.
    for pattern in "$@"; do
        # shellcheck disable=SC2086 # the globs are meant to expand
        set -- $(ls -1 ${pattern} 2>/dev/null)
        if [ "$#" -gt 0 ]; then
            echo "$1"
            return 0
        fi
    done
    return 1
}

LIBMAIN_SO="${MH3_LIBMAIN_SO:-}"
if [ -z "${LIBMAIN_SO}" ]; then
    LIBMAIN_SO="$(find_lib "${BUILD_DIR_NATIVE}/libmain.so" "${BUILD_DIR_NATIVE}/*/libmain.so")" || true
fi

LIBPNG_SO="${MH3_LIBPNG_SO:-}"
if [ -z "${LIBPNG_SO}" ]; then
    LIBPNG_SO="$(find_lib "${BUILD_DIR_NATIVE}/_deps/png-build/libpng16.so" \
                          "${BUILD_DIR_NATIVE}"/_deps/*/libpng16.so \
                          "${BUILD_DIR_NATIVE}"/*/libpng16.so)" || true
fi

LIB_SDL3_SO="${MH3_LIB_SDL3_SO:-}"
if [ -z "${LIB_SDL3_SO}" ]; then
    LIB_SDL3_SO="$(find_lib "${BUILD_DIR_NATIVE}"/_deps/*/libSDL3.so \
                            "${BUILD_DIR_NATIVE}"/_deps/*/libSDL3.so.* \
                            "${BUILD_DIR_NATIVE}"/*/libSDL3.so \
                            "${BUILD_DIR_NATIVE}"/*/libSDL3.so.*)" || true
fi

missing=""
for pair in "libmain.so:${LIBMAIN_SO}:MH3_LIBMAIN_SO" \
            "libpng16.so:${LIBPNG_SO}:MH3_LIBPNG_SO" \
            "libSDL3.so:${LIB_SDL3_SO}:MH3_LIB_SDL3_SO"; do
    name="${pair%%:*}"; rest="${pair#*:}"; path="${rest%%:*}"; var="${rest##*:}"
    if [ -z "${path}" ] || [ ! -f "${path}" ]; then
        missing="${missing}
  ${name}: not found under ${BUILD_DIR_NATIVE} (set ${var})"
    fi
done

if [ -n "${missing}" ]; then
    echo "build_apk.sh: missing native libraries:${missing}" >&2
    echo "Build them first: scripts/build-android.sh (or cmake --build build-android)" >&2
    exit 1
fi

echo "=== 0. Native libraries ==="
echo "  libmain.so  ${LIBMAIN_SO}"
echo "  libpng16.so ${LIBPNG_SO}"
echo "  libSDL3.so  ${LIB_SDL3_SO}"

PKG_DIR="${PROJECT_ROOT}/packaging/android"
BUILD_DIR="${PKG_DIR}/build"
OBJ_DIR="${PKG_DIR}/obj"
SRC_DIR="${PKG_DIR}/src"
RES_DIR="${PKG_DIR}/res"
MANIFEST="${PKG_DIR}/AndroidManifest.xml"
KEYSTORE="${PKG_DIR}/oceantri.keystore"
if [ ! -f "${KEYSTORE}" ] && [ -f "${BUILD_DIR}/oceantri.keystore" ]; then
    KEYSTORE="${BUILD_DIR}/oceantri.keystore"
fi

mkdir -p "${BUILD_DIR}"
mkdir -p "${OBJ_DIR}"

echo "=== 1. Compiling Java Sources ==="
rm -rf "${OBJ_DIR}"
mkdir -p "${OBJ_DIR}"

cd "${SRC_DIR}"
find . -name "*.java" > "${BUILD_DIR}/sources.txt"
javac --release 17 -cp "${ANDROID_JAR}" -d "${OBJ_DIR}" @"${BUILD_DIR}/sources.txt"

echo "=== 2. Creating classes.dex with D8 ==="
cd "${OBJ_DIR}"
find . -name "*.class" > "${BUILD_DIR}/classes.txt"
"${SDK_BUILD_TOOLS}/d8" --output "${BUILD_DIR}/" --lib "${ANDROID_JAR}" --min-api 29 $(cat "${BUILD_DIR}/classes.txt")

echo "=== 3. Compiling Android Resources ==="
"${SDK_BUILD_TOOLS}/aapt2" compile --dir "${RES_DIR}" -o "${BUILD_DIR}/compiled_res.zip"

echo "=== 4. Linking Base APK ==="
rm -f "${BUILD_DIR}/base.apk"
"${SDK_BUILD_TOOLS}/aapt2" link -o "${BUILD_DIR}/base.apk" \
    -I "${ANDROID_JAR}" \
    --manifest "${MANIFEST}" \
    "${BUILD_DIR}/compiled_res.zip" \
    --auto-add-overlay

echo "=== 5. Adding classes.dex to APK ==="
cd "${BUILD_DIR}"
jar -uf "base.apk" classes.dex

echo "=== 6. Preparing and Stripping Native Libraries ==="
mkdir -p "${BUILD_DIR}/apk_root/lib/arm64-v8a"
cp "${LIBMAIN_SO}" "${BUILD_DIR}/apk_root/lib/arm64-v8a/libmain.so"
cp "${LIBPNG_SO}" "${BUILD_DIR}/apk_root/lib/arm64-v8a/libpng16.so"
cp "${LIB_SDL3_SO}" "${BUILD_DIR}/apk_root/lib/arm64-v8a/libSDL3.so"

"${NDK_STRIP}" --strip-all "${BUILD_DIR}/apk_root/lib/arm64-v8a/libmain.so"
"${NDK_STRIP}" --strip-all "${BUILD_DIR}/apk_root/lib/arm64-v8a/libpng16.so"
"${NDK_STRIP}" --strip-all "${BUILD_DIR}/apk_root/lib/arm64-v8a/libSDL3.so"

echo "=== 7. Adding Native Libraries to APK ==="
cd "${BUILD_DIR}/apk_root"
jar -uf "${BUILD_DIR}/base.apk" lib/arm64-v8a/libmain.so lib/arm64-v8a/libpng16.so lib/arm64-v8a/libSDL3.so

echo "=== 8. Aligning APK (16KB page alignment) ==="
cd "${BUILD_DIR}"
rm -f aligned.apk
"${SDK_BUILD_TOOLS}/zipalign" -P 16 -f 4 base.apk aligned.apk

echo "=== 9. Signing APK ==="
if [ ! -f "${KEYSTORE}" ]; then
    echo "=== Generating signing keystore ==="
    keytool -genkeypair -v -keystore "${KEYSTORE}" -alias oceantri -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass oceantrikey -keypass oceantrikey -dname "CN=OceanTri, OU=Gaming, O=Recomp, L=City, S=State, C=US"
fi
OUTPUT_APK="${BUILD_DIR}/${MH3_APK_NAME:-OceanTriRecomp-Android-v1.6-arm64.apk}"
rm -f "${OUTPUT_APK}"
"${SDK_BUILD_TOOLS}/apksigner" sign \
    --ks "${KEYSTORE}" \
    --ks-pass pass:oceantrikey \
    --key-pass pass:oceantrikey \
    --min-sdk-version 19 \
    --v1-signing-enabled true \
    --v2-signing-enabled true \
    --v3-signing-enabled true \
    --out "${OUTPUT_APK}" \
    aligned.apk

echo "=== 10. Copying release APK ==="
DEST_DIR="${PROJECT_ROOT}/release"
mkdir -p "${DEST_DIR}"
cp "${OUTPUT_APK}" "${DEST_DIR}/${MH3_APK_NAME:-OceanTriRecomp-Android-v1.6-arm64.apk}"

echo "=== APK Successfully Built ==="
ls -lh "${DEST_DIR}/${MH3_APK_NAME:-OceanTriRecomp-Android-v1.6-arm64.apk}"
