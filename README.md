# OceanTriRecomp Android 🦖📱

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: Android](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/Stlucifer15t/OceanTriRecomp-Android)
[![Latest Release](https://img.shields.io/github/v/release/Stlucifer15t/OceanTriRecomp-Android?include_prereleases&sort=semver)](https://github.com/Stlucifer15t/OceanTriRecomp-Android/releases)

Native **Android** port of **Monster Hunter Tri** using static ahead-of-time (AOT) recompilation. PowerPC game code is translated to C++ and compiled for ARM64, giving native performance without heavyweight emulation.

This repository is an Android-focused fork of [Kiridor123/OceanTriRecomp-Android](https://github.com/Kiridor123/OceanTriRecomp-Android), which builds on [krisztsa/OceanTriRecomp](https://github.com/krisztsa/OceanTriRecomp). It adds an offline **Cheat Manager**, a package-based **Mod Manager**, touch UI improvements, and Android startup/input fixes.

---

## 🙏 Credits and Base Projects

This project exists because of the work of other developers. Full credit goes to the projects and authors below.

### Base Project: OceanTriRecomp

> **Direct foundation: [krisztsa/OceanTriRecomp](https://github.com/krisztsa/OceanTriRecomp)**
>
> The PowerPC → C++ static recompilation architecture, the modern Aurora HAL renderer, and the native runtime were created by **[krisztsa](https://github.com/krisztsa)**. This repository adapts and extends that work for Android ARM64.

### Android Upstream: Kiridor123/OceanTriRecomp-Android

> This repository is forked from **[Kiridor123/OceanTriRecomp-Android](https://github.com/Kiridor123/OceanTriRecomp-Android)**, which provides the Android packaging, touch controls, online integration, and many device-specific fixes.

### Online Server: MH3SP (Monster Hunter 3 Server Project)

> Online mode (Loc Lac City) uses a server implementation derived from **[sepalani/MH3SP](https://github.com/sepalani/MH3SP)**, created by **[Sepalani](https://github.com/sepalani)**.
>
> - **Server repository**: [sepalani/MH3SP](https://github.com/sepalani/MH3SP) — AGPL-3.0
> - **Client patches**: [sepalani/MH3SP-patcher](https://github.com/sepalani/MH3SP-patcher)
>
> [`mh3sp_native_server.cpp`](runtime/src/mh3sp_native_server.cpp) is a native C++ adaptation of the original Python MH3SP server. Derived code remains under the original AGPL-3.0 license, as noted in its headers. The default online connection points to the community server `mh3sp.com`.

### Recompilation Toolchain: WiiCompiled

> The PowerPC → C++ static translator and ABI bridge come from **WiiCompiled** (commit `85f250155f`).
> See [`toolchain/ORIGIN.md`](toolchain/ORIGIN.md) and [`toolchain/THIRD-PARTY-NOTICES.md`](toolchain/THIRD-PARTY-NOTICES.md) for details and licensing (GPLv3).

### 👥 Community Contributors

- **[Álvaro Barchín (@abarchin)](https://github.com/abarchin)**:
  - **Audio:** Automatic recovery/reconnection of Android audio streams after suspending the app or switching away (`AAudio` lifecycle fix).
  - **Graphics / Effects:** Fixed particle lighting order (`CalcCollected`), removing the white flash/unlit texture effect when hitting monsters or kicking up dust.
  - **Controls:** Option to auto-hide the settings wheel (⚙) after touch inactivity (3 s, 5 s, 10 s).

### Other Projects Used

| Project | Use | License |
|---------|-----|---------|
| [Aurora (encounter/aurora)](https://github.com/encounter/aurora) | SDL3 + OpenGL ES 3.0 graphics HAL | MIT |
| [SDL3](https://github.com/libsdl-org/SDL) | Windowing, audio, cross-platform input | zlib |
| [Dolphin Emulator](https://github.com/dolphin-emu/dolphin) | Hardware behavior reference, Riivolution and HD texture package conventions | GPL-2.0 |
| [Crypto++](https://github.com/weidai11/cryptopp) | Network cryptography (SHA-1, ECDSA) | BSL-1.0 |
| [pugixml](https://github.com/zeux/pugixml) | XML parser for Riivolution patches | MIT |
| [toml11](https://github.com/ToruNiina/toml11) | Settings configuration | MIT |
| [stb_dxt](https://github.com/nothings/stb) | DXT texture compression | MIT/Public Domain |

---

## ✨ New in v1.5.5-test (versionCode 10)

### 🎛️ 1. Offline Cheat Manager

- New **Cheats** tab in the touch settings overlay.
- Curated native cheat categories: Player, Items, Combat, Progression, Advanced, and Gecko/custom codes.
- Whole-row tapping: tap anywhere on a cheat row to open its confirmation/toggle flow.
- Enabling a cheat automatically turns **Master ON** when needed, and the confirmation dialog says so.
- Save-risk warnings for progression/advanced cheats and one-shot box fills.
- Master OFF, all-OFF, latch reset, and repeat one-shot fills.
- Profile import/export under `/storage/emulated/0/MH3/cheats/`.
- Online/City safety guard: cheats cannot be enabled during an online session. The guard now correctly allows **offline village** gameplay; earlier test builds falsely treated village mode as online.

### 🧪 2. Strict Custom RAM-Write Codes

- Supports reviewed Gecko-style `00` byte, `02` halfword, and `04` word writes only.
- Requires explicit source → PAL target mappings; no blind PAL/NTSC offset guessing.
- Rejects ASM, hooks, pointers, conditionals, executable/MMIO targets, and unmapped addresses.
- Custom codes import OFF and are persisted separately from native cheats.
- See [`docs/cheat-and-mod-manager.md`](docs/cheat-and-mod-manager.md) for format and safety details.

### 📦 3. Mod Manager

- New **Mods** tab for managed replacement packages.
- Packages live under `/storage/emulated/0/MH3/mods/<package-id>/package.oceantri`.
- Manifest validation, SHA-256 integrity checks, exact-length file replacement checks, and unsafe-path rejection.
- HD texture packs can activate/deactivate live.
- Model/weapon/armor BRRES and audio stream replacements are saved immediately but apply at the **next launch** when required.
- Priority-based conflict resolution and isolated mod asset caches.
- Original disc files and Wii saves are never edited by the Mod Manager.

### 📱 4. Android Startup and Input Fixes

- Fixed the Android launch abort caused by mismatched SDL JNI runtimes (`JNI_OnLoad` / missing `onNativePadDown`).
- The APK now uses a single vendored **SDL 3.4.16 static** runtime inside `libmain.so`; no separate `libSDL3.so` is packaged.
- Android keyboard/IME customizations are preserved.
- Added configure-time and APK-time SDL JNI contract verification.

### ⚠️ Test-Build Notes

- This is a **test build**: `versionName=1.5.5-test`, `versionCode=10`.
- Only the PAL disc `RMHP08` is supported by the runtime manager paths.
- Cheats can modify save data. Back up `/storage/emulated/0/MH3` before experimenting.
- CI builds may be signed with a fresh ephemeral key. If Android refuses an update, back up your data, uninstall the previous CI APK, and reinstall.

---

## ✨ v1.5 Features

### ⌨️ 1. Native Android Keyboard Integration

- **Automatic opening:** When the game opens a text-entry dialog (character creation/name, Loc Lac chat, equipment names, etc.), the system keyboard opens automatically.
- **Real-time typing/deleting:** Text is written directly into the game’s UTF-16BE buffer, including Spanish characters (`ñ`, accents, numbers, and punctuation).
- **Enter confirmation:** Pressing Enter/Send confirms the text natively and closes the Wii menu.
- **Smart virtual-control hiding:** On-screen buttons hide while the keyboard is active and return when it closes.
- **Background restore:** If you leave the app with the keyboard active, it restores smoothly when focus returns.
- **Game-control isolation:** PC control keys are blocked while typing, preventing letters/space from moving the hunter or triggering buttons.

### ⏭️ 2. Cutscene Skip with A

- Skip cutscenes in real time by pressing **A** or **+**.
- Works for both in-engine sequences (monster introductions, in-game cinematics) and pre-rendered THP videos.

### 🚀 3. Low-End Performance / Slow-Motion Fix

- Fixed artificial slow motion on devices running at 15–25 FPS.
- Increased the dynamic frame delta limit to 100 ms (10 FPS), allowing physics/animation to stay synchronized in real time at low frame rates.

### 🛡️ 4. 4 GB RAM Device Crash Fix

- Fixed `Invalid or duplicate model GPU registration` crashes when screen recording or resuming on some MediaTek Helio G85 devices.
- Safer mesh-handle management and draw-cycle purge handling.

### 📺 5. Aspect Ratio Selector

- In `Settings` → `Graphics`: **16:9 Widescreen (Recommended)**, **Adaptive Fullscreen**, and **4:3 Classic**.
- Fixes misaligned black bars in menus.

### ⚠️ Known Issues

1. **Water shortcut in “No Love for Ludroth”**: In the 2-star village quest *Sin amor para el Ludroth*, the base-camp water shortcut can have physics problems. Other quests have not shown this issue.
2. **Keyboard autocomplete**: Using predictive text/autocomplete may visually corrupt the in-game text box. Typing letter by letter is recommended.

---

## ✨ Previous Versions

### 🏰 v1.4.5 Highlights

#### 🏰 1. Online Mode — Loc Lac City

- Direct connection to the community server **mh3sp.com**.
- Embedded local server (native adaptation of [MH3SP](https://github.com/sepalani/MH3SP)) for LAN mode.
- LAN beacon for automatic local/VPN discovery (**ZeroTier**).
- Native NAS (Nintendo Authentication Server) implementation.

#### ⏱️ 2. Free FPS Selector (30 / 60 / 90 / 120 FPS)

- In `Settings` → `Graphics`:
  - **30 FPS:** Original Wii physics speed; recommended for online stability.
  - **60 / 90 / 120 FPS:** Smoother visuals on high-refresh displays.

#### 🔇 3. Background Audio Fix (AAudio)

- Fixed permanent audio loss after switching apps or turning the screen off. Audio resumes when returning to the game.

#### 💥 4. Combat Flash / Particle Fix

- Fixed white flash/star effects when hitting monsters or running on dirt by applying lighting in the correct order before particle emitters.

#### 📊 5. FPS and Device Temperature HUD

- Optional bottom-left HUD showing current FPS and device/battery temperature in °C.

#### 🎚️ 6. Touch Opacity and Auto-Hide Settings

- In `Settings` → `Controls`:
  - **Touch opacity:** 25%, 50%, 75%, or 100%.
  - **Auto-hide settings wheel:** 3, 5, or 10 seconds.
- Preferences are saved in `touch_layout.cfg`.

#### 🖼️ 7. Dolphin-Style HD Texture Replacement

- Supports Dolphin HD texture packs (`tex1_...`).
- Place texture folders such as `RMHE08` or `RMHP08` in:
  `/storage/emulated/0/MH3/textures/`
- `Settings` → `Graphics` includes an HD texture toggle, loaded-texture counter, and hot-reload button.

#### 🔐 8. Triple APK Signing (v1 + v2 + v3)

- APKs are signed with v1, v2, and v3 schemes and aligned for 16 KB pages (`zipalign -P 16`).
- Improves compatibility across MIUI, HyperOS, ColorOS, One UI, OxygenOS, and AOSP.

#### 🎮 9. Touch Controls and Physical Controllers

- Full virtual gamepad: analog sticks, A/B/X/Y, L/R/ZL/ZR, D-Pad, +/-.
- Macro buttons: M1 (A+X), M2 (B+Y), M3 (L+R), M4 (A+B).
- On-screen edit mode for dragging/resizing buttons.
- Bluetooth/USB controller keymapper with Nintendo and Xbox/generic layouts.

#### 📝 10. Logging

- Logs are stored in `/storage/emulated/0/MH3/logs/`:
  - `game_log.txt`: Game output/activity.
  - `oceantri_log.txt`: Runtime/network events.
  - `oceantri_crash.txt`: Crash diagnostics.

---

## 📲 Installation

1. Go to **[Releases](https://github.com/Stlucifer15t/OceanTriRecomp-Android/releases)**.
2. Download `OceanTriRecomp-Android-v1.5.5-test-arm64.apk`.
3. Install it on your Android device. Allow installation from unknown sources if prompted.
4. Open the app and select your legally obtained copy of **Monster Hunter Tri** (`.iso` or `.rvz`; European PAL `RMHP08` is recommended/required for manager features).
5. Hunt!

> **Updating from an older CI/test APK:** CI signing keys may differ between runs. If installation fails with a signature mismatch, back up `/storage/emulated/0/MH3`, uninstall the old APK, then install the new one.

---

## 🎛️ Cheat and Mod Quick Start

### Cheats

- Open the touch settings overlay and go to **Cheats**.
- Enable a cheat; Master turns ON automatically if needed.
- Confirm save-risk warnings carefully.
- Profiles and custom codes live under `/storage/emulated/0/MH3/cheats/`.
- Cheats are blocked in online/City sessions. Return offline and relaunch for a clean guard.

### Mods

- Place a package at `/storage/emulated/0/MH3/mods/<package-id>/package.oceantri`.
- The folder name must match the manifest `id`.
- Open **Mods** and enable the package.
- HD textures apply live; model/audio file replacements may require relaunch.
- See [`docs/cheat-and-mod-manager.md`](docs/cheat-and-mod-manager.md) for manifest format, safety rules, and troubleshooting.

---

## 🛠️ Device Requirements

- **CPU:** 64-bit ARMv8-A (Snapdragon, MediaTek Dimensity, Google Tensor, Samsung Exynos, etc.).
- **OS:** Android 10 (API 29) or newer.
- **Graphics:** OpenGL ES 3.0+.
- **Storage:** Around 3–5 GB free for game data and optional textures/mods.

---

## 🏗️ Build from Source

### Prerequisites (Linux)

- Android NDK r28+
- Android SDK Build-Tools 35.0.0
- CMake 3.25+ and Ninja
- Clang / LLVM
- Java JDK 17+
- .NET 8 SDK (for the translator CLI)

### Steps

```bash
# 1. Clone the repository
git clone https://github.com/Stlucifer15t/OceanTriRecomp-Android.git
cd OceanTriRecomp-Android

# 2. Set environment variables (adjust paths for your machine)
export ANDROID_HOME="/path/to/android-sdk"
export ANDROID_NDK_HOME="${ANDROID_HOME}/ndk/28.2.13676358"

# 3. Configure CMake for ARM64
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release \
  -DANDROID=ON \
  -G Ninja

# 4. Build the native target
cmake --build build-android --target MH3 --parallel "$(nproc)"

# 5. Package the signed APK
bash packaging/android/build_apk.sh
```

The signed APK is generated at:

```text
release/OceanTriRecomp-Android-v1.5.5-test-arm64.apk
```

### Host Tests

```bash
cmake -S tests/manager -B build-manager/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-manager/tests --parallel "$(nproc)"
ctest --test-dir build-manager/tests --output-on-failure
python3 -m unittest discover -s tests/android -p 'test_*.py' -v
```

---

## ⚖️ License and Disclaimer

### Code License

This project is distributed under the **GNU General Public License v3.0 (GPLv3)**. See [LICENSE](LICENSE) for details.

### Legal Disclaimer

- **Monster Hunter** and **Monster Hunter Tri** are trademarks of **Capcom Co., Ltd.**
- **Wii** is a trademark of **Nintendo Co., Ltd.**
- This is an independent open-source project for research, compatibility, and historical preservation. It is not affiliated with, endorsed by, or sponsored by Capcom or Nintendo.
- **This repository and its compiled releases do not contain or distribute copyrighted game assets, proprietary code, models, textures, music, or ROM files.** Users must provide their own legally acquired copy of *Monster Hunter Tri* to generate/load the required game data.
