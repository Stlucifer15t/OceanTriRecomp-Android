# Cheat Manager and Mod Manager

This branch adds an integrated, offline-first Cheat Manager and runtime Mod Manager to the existing Android touch overlay. It reuses the project's native PAL cheat ports and Aurora asset paths instead of replacing the renderer, save system, or recompilation toolchain.

## Repository analysis that drove the design

- `recomp.yml`, `generated/`, and `toolchain/translator/` describe an ahead-of-time PowerPC-to-C++ translation of PAL **RMHP08**, not a JIT or a general ARM64 Wii emulator. Guest data remains in a big-endian Wii address space; host ARM64 pointers and translated function addresses are not Wii code addresses.
- `runtime/src/game_frame.cpp` runs cheat work on the game thread. `runtime/src/cheat_*.cpp` already contains native replacements/wrappers for many original US Gecko behaviors, manually matched to PAL addresses. These are the reliable reuse points.
- `runtime/src/cheats.cpp` already had a large native table and CLI entry points. The live manager removes the old cached "any cheat enabled" decision, gates flags through a master switch, preserves CLI selections, and keeps button-latched state resettable.
- `runtime/src/touch_controls.cpp` is the Android GLES3 overlay. The new Cheats and Mods tabs live there. UI/input threads only queue commands; guest memory and native cheat state change on the game thread.
- `runtime/src/net/online_patches.cpp` refuses online admission after cheat use. The guard is sticky for the launch: turning cheats OFF does not launder already-changed money, equipment, items, quest flags, or custom RAM writes.
- Aurora's `texture_replacement` already indexes PNG/DDS replacements by texture/palette hash. Managed mod roots are added ahead of the legacy fixed roots only when a managed package is active; with no managed packages the previous loose-texture behavior is unchanged.
- `runtime/src/asset_cache.cpp` converts BRRES assets and copies audio streams. A simple `DiscImage` overlay would be bypassed by cached converted assets, so runtime file replacements are installed before conversion and active file selections receive a separate hash-named asset cache.
- Saves remain the original Wii NAND/GCI structures. Cheat/mod configuration is stored separately under the user directory and never replaces a save file.

## Implemented Cheat Manager

### Native switches

The manager exposes curated categories plus the existing advanced native table:

- **Player:** infinite health, stamina, oxygen, sharpness.
- **Items:** unlimited consumables, unlimited ammunition, item-box quantities.
- **Combat:** one-hit kills, maximum damage, no sharpness loss.
- **Progression:** maximum money, weapon/armor box unlock, quest-tier unlock, item-box fill.
- **Advanced:** remaining non-typed native cheats, with save-risk warnings.
- **Gecko:** explicitly mapped custom RAM-write codes.

The shared sharpness switch appears in Player and Combat but controls one native ID. Progression writes and advanced codes are marked as save risks. One-shot box fills warn that OFF does not remove items/equipment already written. Typed-name cheats remain CLI-only (`--cheat`) rather than being exposed as touch toggles that would lack their required text.

Runtime state:

- `CheatManager::Initialize` runs after CLI parsing and before guest memory initialization. A corrupt saved profile falls back to OFF with a warning instead of preventing boot.
- UI commands use a bounded queue. `CheatManager::Pump` runs immediately before `Cheats::Step` on the game thread.
- Master OFF stops native per-step writes and gates hook flags. It does not revert save changes.
- `Repeat` requeues enabled one-shot box fills. `Reset` clears button-activated cheat latches.
- Configuration is `OCEANTRI-CHEATS 1`, game `RMHP08`, native index/enabled/choice rows, and optional custom-code identities. Import always forces master OFF for review.

Files under `<MH3_USER_DIR>/cheats/`:

- `active.oceantri` — autosaved native + custom selection.
- `import.oceantri` — touch Import source.
- `export.oceantri` — touch Export destination.
- `custom-active.oceantri` — custom-code switch persistence.
- `gecko/*.txt` and matching `gecko/*.map` — custom-code library.

On Android the user directory is normally `/storage/emulated/0/MH3`.

## Gecko / custom-code compatibility

Full Gecko compatibility is **not** claimed. Dolphin's normal Gecko path installs a PowerPC code handler and a GCT list into emulated Wii memory and executes it through the emulator ([GeckoCode.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/GeckoCode.cpp)). This project statically translates PAL code; writing PPC instructions or running the Gecko handler would not patch the translated ARM64 executable. The ordinary write codetypes are visible in Dolphin's handler reference ([codehandler.s](https://github.com/dolphin-emu/dolphin/blob/master/docs/codehandler.s)).

The implemented alternative is a strict, reviewed RAM-write importer:

- Supported: non-repeat Gecko `00` byte, `02` halfword, and `04` word writes.
- Rejected: ASM/C2, hooks, pointers, base-address operations, conditionals/controller codes, repeats, serial/string writes, terminators, executable targets, MMIO, and unmapped addresses.
- Every write needs an exact source-address/width → PAL-target-address mapping. The importer never applies a blanket PAL/NTSC offset.
- Source may be declared `RMHP08`, `RMHE08`, or `RMHJ08`; target must be `RMHP08`, the only disc this runtime supports.
- Targets must be aligned data RAM: PAL MEM1 data area or MEM2 heap. The runtime still checks the exact allocation before writing.
- Codes import OFF. OFF stops future writes; it does not pretend to restore previous RAM.
- Custom use marks the launch as cheat-used for online admission.

One code per `.txt` file, with a matching `.map` sidecar:

```text
$Example mapped write [author]
0265AA62 00001234
```

```text
OCEANTRI-GECKO-MAP 1
source RMHE08
target RMHP08
map 8065AA62 8065CE62 2
```

The example is a format template, not a recommended gameplay cheat. Replace addresses only after verifying the source code and the PAL destination. A missing or mismatched mapping rejects the whole package.

## Implemented Mod Manager

Packages live under `<MH3_USER_DIR>/mods/<package-id>/package.oceantri`. The directory name must match the manifest ID. Original disc files and Wii saves are never edited.

Manifest format:

```text
OCEANTRI-MOD 1
game RMHP08
id example-pack
name Example Pack
version 1.0
author You
description Texture/model/audio replacement example
priority 10
textures textures
file 15/path/original.brres files/replacement.brres <64-hex-sha256> <exact-byte-count>
file 16/srt/original_stream files/replacement_stream <64-hex-sha256> <exact-byte-count>
```

Supported package content:

- **HD textures:** PNG/DDS files using Aurora/Dolphin replacement naming. Managed roots are selected by priority and reloaded on the GL thread. Individual activation/deactivation is live.
- **Models / weapon / armor resources:** exact-length BRRES replacements. These cover texture/model changes inside BRRES containers, including weapon/armor resources, and are converted by the existing native asset pipeline.
- **Audio:** exact-length `16/srt/` stream replacements. The replacement must match the original stream format expected by the game; the manager validates length and integrity, not audio semantics.
- **Custom packages:** a manifest can combine texture roots, BRRES files, and audio streams.

Safety and cache behavior:

- Paths must stay inside the package; traversal, absolute paths, backslashes, and symlinks are rejected.
- File replacements are SHA-256 checked at scan and again before installation.
- Runtime file overlays are memory-only and capped at 128 MiB total active replacement bytes.
- File replacement targets must exist in the FST and have the exact original length. This preserves guest file offsets and existing loader expectations.
- Active file selections get `<MH3_USER_DIR>/mod-cache/<hash>/cache`, keyed by package IDs and replacement hashes. The original asset cache is not reused for modified sources.
- Model/audio file activation changes are saved immediately but apply at the **next launch**, because converted assets and guest file reads are already in flight. The UI marks these as pending restart.
- If prepared native assets (`HasNativeTextureMap` or `MH3_NATIVE_ASSET_DIR`) bypass BRRES conversion, runtime BRRES/audio overlays fail closed with a visible warning. Texture packs still work. Reprepare native assets with the desired mods for that workflow.
- Compiled executable mods, Kamek patches, RSO overlays, and Riivolution code patches remain build-time toolchain operations. The runtime manager does not load native libraries or patch translated code from a package.

Activation is stored in `<MH3_USER_DIR>/mods/active.oceantri`. Higher priority wins conflicts; equal priority is ordered by package ID. Invalid packages remain visible but cannot be activated.

## Touch interface

The existing settings overlay gains **Cheats** and **Mods** tabs:

- Master switch, all-OFF, latch reset, repeat unlocks.
- Category chips, search with Android/SDL text input, pagination, mouse-wheel paging.
- Individual ON/OFF switches and native choice +/- controls.
- Save-risk / one-shot confirmation before enabling risky writes.
- Cheat import/export buttons and custom-code rescan.
- Mod filters (All / Active / Textures / Files / Issues), search, rescan, individual activation, pending-restart and conflict/error details.
- Opening/closing the menu clears held touch buttons/sticks and camera deltas so gameplay input cannot stick.

The interface is still offline-first. The existing online admission guard remains the enforcement point.

## Verification

Isolated host suites live in `tests/manager` and require Clang:

```sh
cmake -S tests/manager -B build-manager/tests -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build-manager/tests --parallel 2
ctest --test-dir build-manager/tests --output-on-failure
```

Current suites:

1. `mh3_cheat_manager_tests` — catalog, live gates, Step dispatch, profiles, CLI merging, bounded queue, custom profile round-trip, failed-import isolation.
2. `mh3_cheat_effects_tests` — real gameplay hooks against synthetic PAL guest RAM: health, stamina, oxygen, sharpness refill, consumables, ammunition, damage, money, OHK/capture-floor behavior, box-fill orchestration.
3. `mh3_custom_codes_tests` — strict Gecko subset parser, explicit mapping requirement, executable/MMIO rejection, big-endian RAM writes, master/offline gates, persistence.
4. `mh3_mod_manager_tests` — manifest safety, SHA-256 known vector, activation persistence, managed texture roots, partial-read overlay, cache namespacing, length/integrity failure, prepared-native-asset refusal.

The touch overlay was also syntax-checked against SDL3 and GLES3 headers. Full Android compilation and device/game-image playtesting remain CI/user acceptance gates; no licensed ROM, Android SDK/NDK, or device was available in the development workspace.

CI:

- `.github/workflows/manager-tests.yml` runs the isolated host suites on pushes and PRs.
- `.github/workflows/build-apk.yml` now also runs on `arena/**` pushes and PRs to `main`, runs the host suites, builds the ARM64 APK, and uploads an artifact. Release publication remains restricted to pushes on `main`.

### Touch activation and online lock

Device testing after the SDL fix showed two UX problems: taps on a cheat row’s text did nothing because only the small OFF button was hit-tested, and confirming a save-risk cheat left Master OFF, so the switch looked selected but native hooks remained gated. The touch UI now hit-tests the whole row, and enabling a native/custom cheat queues Master ON first when Master is OFF. The confirmation dialog says when Enable will turn Master ON. The game thread also mirrors the City/online guard into `CheatManager::Snapshot::online`; the Cheats tab shows an explicit online lock instead of silently refusing. Cheats remain intentionally unavailable in online/City mode; return offline and relaunch for a clean guard.

### Android SDL startup contract

A device log from the first manager APK showed `SIGABRT` inside `JNI_OnLoad` before `main()`: native SDL registered `SDLControllerManager.onNativePadDown(II)Z`, while the packaged Java glue declared `(III)Z`. The cause was packaging, not the cheat/mod UI: Aurora statically linked SDL 3.4.4 into `libmain.so`, while CI also copied a separate SDL 3.4.16 `libSDL3.so` from an old release and the Java sources were 3.4.16.

Android now uses exactly one SDL runtime: vendored SDL 3.4.16, statically linked into `libmain.so`, with a pinned source-archive SHA-256. The redundant prebuilt `libSDL3.so` is no longer downloaded, packaged, or loaded. `scripts/verify_android_sdl.py` runs during Android CMake configuration and compares the native `SDL_android.c` JNI registration tables against the vendored Java native declarations; it also checks version constants and rejects a separate `libSDL3.so` load/APK entry. `tests/android` contains regression tests for the exact two-argument vs three-argument pad-callback mismatch.

## Remaining device acceptance gates

- Playtest each curated cheat in a real hunt, including area transitions, equipment stat refresh, capture/kill behavior, and save/reload.
- Verify progression changes against a backed-up save and confirm online admission is refused after use.
- Test a real exact-length BRRES weapon/armor replacement and audio stream through the Android asset cache, including activation/deactivation across launches.
- Test managed HD texture priority against legacy loose texture folders.
- Confirm touch search/IME behavior on target Android devices and that menu input never leaks into gameplay.
