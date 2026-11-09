# Cheat Manager and Mod Manager (Android)

This document describes the architecture of the integrated **Cheat Manager** and
**Mod Manager** added to OceanTriRecomp Android, and the reasoning behind the
implementation choices — in particular why original Wii Gecko codes cannot be
executed verbatim on the ARM64 recompilation and how the shipped Gecko loader
deals with that.

Everything described here is implemented in the runtime (`runtime/src/`), works
fully offline, and keeps the original game files untouched.

---

## 1. What the game is, and why a cheat is not a memory poke

Monster Hunter Tri is not emulated here. The PowerPC machine code of
`main.dol` is **translated ahead of time into C++** (`generated/functions/`,
23 170 functions) and compiled for ARM64. The runtime
(`runtime/src/`) contains native re-implementations ("ports") of a few hundred
guest functions, an ABI bridge (`abi_bridge.h`), a flat 32-bit guest memory
window (`guest_flat_memory.cpp`, real address space mapping of 0x80000000 /
0xC0000000 / 0x90000000 aliases) and the Aurora graphics HAL.

Consequences that drive every design decision below:

| Wii / Dolphin behaviour | In this recompilation |
|---|---|
| A Gecko code writes a word into RAM (e.g. `04057E2C 60000000`). | Works — guest memory is real, writable RAM. **Data patches** stay valid. |
| A Gecko `C2` code injects ASM at a code address. | **Impossible** — that address is not executed by an interpreter; the ARM64 code was generated at build time. |
| An `04` write to a code address silently changes behaviour. | The write either hits the loaded `main.dol` image (which is *not* where the CPU executes from) or correctly trips the executable guard, which refuses writes into translated code (`executable_guard.cpp`). |
| `28xxxxxx` button activator lines | Guest input state is real (`Psw+0xD6`); readable every frame. Works. |
| Pointer lines (`8A`, `86`, `4A` …) | Plain guest-memory arithmetic. Works. |
| Memory **addresses** | Identical to the PAL disc (RMHP08) — the DOL is the same file; the recompiler does not relocate it. So PAL addresses are correct, and the well-known *-0x64* offset is **not** what distinguishes regions here. |

So the honest answer to "can original Gecko codes run?" is:

* **Data-write codes (`00/02/04/06` + `28` activators + pointer lines)** — yes,
  they are fully executable, because they only touch guest data memory.
* **Assembly-injection codes (`C2/C6/D2/D6`)** — no, not as machine code. They
  must be *ported*: either the code's intent is re-expressed as a native
  wrapper/hook of the corresponding translated function, or the injected
  PowerPC block is **interpreted** at runtime (which the Gecko loader can do for
  small self-contained blocks, see §3.3).
* **Region** — the PAL disc (RMHP08) is the shipped target, and its addresses
  are the ones the game uses here. NTSC-U/NTSC-J codes carry US/JP addresses;
  the established US→PAL delta for this title is **-0x64** (verified against the
  port table in `cheat_*.cpp`, 51 pairs, one outlier that the port table already
  documents). The Gecko importer applies that delta when the code list declares
  a US/JP region, and refuses to touch addresses outside MEM1/MEM2.

### The existing native cheat framework

`runtime/src/cheats.h` already defines **261 cheat ids** ported by hand from the
RMHE08 Gecko lists (`NoDamage`, `InfiniteAmmo`, `MaxMoney`, `OneHitKills` …),
each implemented as native code (a per-step write, a hook, or a wrapper around a
translated function), with values/options and button activators. Nothing in that
system is replaced by this work; it is extended:

* a **save/load configuration layer** (profiles, import/export),
* a **category + search index** over the existing table (Player / Items /
  Combat / Progression / World / Movement / Online …),
* a **live in-game manager UI** (touch friendly, on top of the existing
  `touch_controls.cpp` overlay engine),
* the **Gecko runtime** (`gecko_vm`, `gecko_codes`, `gecko_db`) with a
  transliteration of the existing native ports back into importable code form,
* the **Mod Manager** (`mod_manager`) and its overlay UI, with texture, model,
  audio, patch and data modifications.

---

## 2. Cheat Manager

### 2.1 Categories

The 261 existing toggles are classified into the categories the product asks for
plus a few that fall out of the table:

| Category | Contents |
|---|---|
| **Player** | infinite health / stamina / oxygen, no damage, sharpness, statuses and resistances, health-related misc. |
| **Items** | unlimited consumables, ammo, item quantities, item box/pouch cheats, gathering/carving. |
| **Combat** | one-hit kills, maximum damage, attack/defence, weapon durability (sharpness), monster HP/rage, hit reactions. |
| **Progression** | money, HR/HRP, quests, quest box, quest rewards, equipment unlocking, smithy, farm/village. |
| **World** | map, HUD, monsters on map, quest clock, weather, traps, farcaster. |
| **Movement** | movement/jump/evade/speed codes, free fly, moon jump, glider, teleport. |
| **Online** | codes that only make sense connected (kept separate, off by default). |
| **Misc** | gallery clips, language, menu cosmetics, character creator, custom (Gecko-imported) cheats. |

Classification lives in `cheat_catalog.cpp` as a table keyed by the *cheat name*
(not the enum), with a deterministic fallback keyword classifier, so an entry
added to the launcher table later still lands in a sensible category. A unit
test asserts every entry of `LauncherCheats::List()` has a category and that the
explicit table has no stale rows.

### 2.2 Configuration, profiles and import/export

`cheat_config.cpp` serialises the full state (`on/off`, selected value, typed
text, plus Gecko and mod activation) to:

```
$MH3_USER_DIR/cheats/profiles/<name>.txt     active profile (text, diff friendly)
$MH3_USER_DIR/cheats/active.txt              name of the profile loaded at boot
$MH3_USER_DIR/cheats/export/<stamp>.txt      exports for sharing
```

Format (line based, `#` comments, values quoted when needed):

```
MH3CHEATS 1
CHEAT "Infinite health" 1 0
CHEAT "Max money" 1 0
GECKO "Max zenny" 1
MOD "HD Skins" 1
```

Loading validates every name against `LauncherCheats::List()` and ignores
unknown rows with a warning, so a config from a newer version never breaks the
game. Import reads a file chosen in the UI (or dropped in
`$MH3_USER_DIR/cheats/import/`), sanitises it and applies it live.

The profile is applied at startup *after* `--cheat` command line processing, so
command-line cheats win.

### 2.3 UI

The manager is a new top-level tab in the existing touch overlay
(`God Mode / Cheats`, next to Graphics, System, Saves, Controls, Online). It
reuses the overlay's SDF renderer (`DrawRoundedRect`, `DrawText`,
`RegisterHitZone`, `DrawInteractiveButton`, scissor clipping and inertial
scrolling), so it costs no new GPU state and keeps the Laguna-de-Moga theme.

Sections:

* **Live**: category chips, a search field, virtualised rows with a toggle per
  cheat, a value selector for cheats that take one.
* **Gecko**: an editable code list, import from clipboard/file, per-code enable,
  region selector.
* **Mods**: installed mods with checkboxes, dependency/patch status.
* **Config**: profile list, save/load, import/export, "reset all".

The search field uses a built-in on-screen keyboard drawn by the overlay
(`cheat_ui_keyboard`), so no platform IME is required — it works the same on all
Android builds.

---

## 3. Gecko support

### 3.1 Module layout

| File | Responsibility |
|---|---|
| `gecko_vm.h/.cpp` | Guest memory access façade used by the VM: resolves PAL/US addresses, validates ranges, and provides the only place a cheat may write. |
| `gecko_parser.cpp` | Parses the textual Gecko format (8 hex digits per line, `$`/`*` comments), validates opcodes, canonicalises. |
| `gecko_engine.cpp` | Executes codes once per logic step (from `Cheats::Step`): data writes, activators, pointer chains, conditional (if/endif) blocks, repeat (`*`), and C2/C6/D2/D6 handling. |
| `gecko_asm.cpp` | Compact **PowerPC interpreter** for C2/C6 injected blocks and D2/D6 float blocks: a bounded subset sufficient for the code lists (integer/float load-store, ALU, compares, branches, `blr`). |
| `gecko_db.cpp` | Bundled, offline database of the original published code lists (PAL, US, JP where known), grouped by author, with the port status of every code (`native`, `data`, `asm`, `unsupported`). |

### 3.2 Execution model

`Cheats::Step(ctx)` calls `Gecko::Step()` once per logic step, before the native
cheat passes, mimicking the per-frame behaviour of Dolphin's code handler.
Per code:

1. If the code has activator lines (`28xxxxxx MMMMXXXX`), evaluate them against
   the live controller state. The activator's *type* byte chooses the trigger:
   `00`/`04` = equal / not equal on value, `08`/`0C` = AND/AND-NOT mask,
   `10`/`14` = etc. — Gecko's documented activator semantics are implemented,
   including the "always" variant (`04`).
2. If not satisfied, skip the code this step (this is what makes the same
   toggles work as one-shot presses).
3. Otherwise run the body:
   * `00/02/04/06` — direct write (8/16/32-bit, plus `06` for 32+8 byte pair).
   * `8A/88/86/84` — set base address + offset.
   * `4A/48/46/44` — add to base address.
   * `2A/28/26/24` — set/add to a "value" register? (Gecko uses these only for
     activators; the runtime treats them as no-ops outside activator context.)
   * `C2/C6/D2/D6` — assembly injection: locate the address, fetch the original
     PowerPC block from the loaded DOL image (real bytes are present in guest
     memory), and install it as an *emulated instruction sequence* hooked at
     the corresponding native entry point. See §3.3.
   * `E0` — Gecko "end of code" marker, `F0` — "no next codes" guard.
4. All writes go through `Gecko::Write*`, which refuses addresses outside
   MEM1/MEM2 and out of the loaded DOL's writable data ranges (never translated
   code).

### 3.3 C2/C6/D2/D6: the honest answer, and what we do

The recompiler emits one ARM64 function per PowerPC function, and the ABI bridge
keeps a map from guest entry point → generated function
(`REGISTER_NATIVE_FUNCTION`, `native_index.json`). A `C2` code conceptually
replaces a few instructions inside function `F` with a branch into a code cave.
There are three possible translations:

1. **Native hook** — `F` (or the containing function) already has a native port
   (`runtime/src/*.cpp`); the code's intent is folded into it. This is what the
   261 shipped cheats do, and it stays the preferred path.
2. **Instruction-level emulation** — install a *hooked* entry: when the injected
   block is a small, self-contained sequence that only touches registers provided
   by the caller and memory, the runtime executes it in the PowerPC interpreter
   at the same point of the frame, with the caller's register file captured from
   the native call. The interpreter covers the subset used by the published
   MH3 code lists (verified by the parser's opcode census, see
   `gecko_asm.cpp`).
3. **Refuse, with a reason** — everything else is flagged `unsupported` in the
   UI with the reason ("injects into a function with no native port", "writes
   translated code", "branch target outside DOL"). No silent failure.

Because (2) needs a register file at the hook point, the engine only accepts an
injection whose site is the *entry* of a translated function (the common case
for `C2` in MH3 lists: "replace the first instruction, then execute the
original"). When the injected site is in the middle of a function, the code is
classified `unsupported: interior injection` and its author-supplied intent is
carried by the bundled DB as a native port if one exists.

> **Never blindly trust Wii addresses.** Every address is validated against
> MEM1/MEM2/target region before any read or write; the loader computes region
> deltas explicitly and shows the resulting PAL address to the user.

### 3.4 Region handling

* The active region is inferred from the loaded disc (`recomp.yml` is PAL,
  `RMHP08`), and shown in the UI.
* Imported code text can carry `#region us` / `#region jp` markers; the importer
  also guesses from a small heuristic (address present in the bundled US table)
  and asks the user when ambiguous.
* Conversion: US→PAL `-0x64`, JP→PAL `unknown` (refused, flagged, user can
  force with a per-code numeric delta).
* The delta is applied to code addresses (writes, pointer bases, injection
  sites), never to activator button masks.

### 3.5 Bundled database

`gecko_db.cpp` ships one entry per cheat of the port's table (261), in the
catalog's categories rather than the original authors' tags, so the Gecko tab
and the native list describe the same set of changes. Offline, no network: the
entries are built from the port's own documented addresses and field offsets.

Each entry has one of two shapes, and the difference is what a static
recompilation forces, not a shortcut:

* **A data code.** The port's implementation is a plain write of a fixed value
  to a fixed save-data field, so the same write is encoded in Gecko's own
  notation and the runtime executes it once per logic step. Save data is reached
  through the pointer variable (`48` loads `po` from `0x80796D40`, then
  `10`/`12`/`14` write through it), because the game moves that data.
  19 of the 261 are data codes today.
* **A native-backed entry.** The published code hooks or patches a game
  function, needs a value the player picks, or chases a pointer through the
  operand flags this engine refuses rather than guesses at. ARM64 code was
  generated at build time, so such a code cannot run here at all; the entry is
  listed so the cheat is visible and the built-in port is the implementation.
  `native` names that port in the list.

A test asserts the two counts, that every data code runs against a fake RAM, and
that each of them writes through `po` (into the block the pointer variable
points at) rather than at a fixed address — so an entry that claims to be a
running code is one.

---

## 4. Mod Manager

### 4.1 Asset-loading architecture (as found)

```
            ┌──────────────┐  raw partition bytes
 ISO/RVZ ──►│  RomImage    │──────────────────────┐
            └──────────────┘                      │
                    │                             ▼
                    │            ┌───────────────────────────────┐
                    └───────────►│ DiscImage (file table, reads) │
                                 └───────────────────────────────┘
                                        │              │
                     texture/model source files  16/srt/* audio
                                        ▼              ▼
                        ┌──────────────────────┐  ┌──────────────┐
                        │ AssetCache (convert) │  │ cache/files  │
                        │ textures/ models/    │  │ (extracted)  │
                        └──────────────────────┘  └──────────────┘
                                        │
                          ┌─────────────┴──────────────┐
                          ▼                            ▼
               aurora native texture packs     model packs (M3MP)
               open_native_texture_pack()      LoadNativeModelAssets()
```

Override points already present in Aurora:

* `texture_replacement` — scans `graphics/texture_replacements` **plus**
  `MH3_TEXTURE_DIR` **plus** `/storage/emulated/0/MH3/textures` for
  `tex1_<w>x<h>_<texhash>[_<tluthash>]_<fmt>.dds|.png` and replaces textures
  at bind time. `set_enabled()/reload()` exist and are already exported.
* Aurora's `NativeTexturePatch` hook (`set_native_texture_patch`) lets the
  runtime rewrite any decoded native texture in place — used today for button
  glyphs; it is the cheapest way to apply *procedural* modifications.
* `DiscImage::SetFileOverride(name, bytes)` — per-file overlay used by the
  launcher for quest overlays; it applies to partial DVD reads as well.
* `AssetCache` — converts texture and model source files to native packs keyed
  by partition offset; `ModelPath`/`TexturePath` describe the written packs.

### 4.2 mod_manager design

```
$MH3_USER_DIR/mods/
  <mod-id>/
    mod.toml            metadata: name, version, author, game, requires, conflicts
    textures/           tex1_* files (DDS or PNG) — texture replacements
    audio/              16_srt_<offset>.s3d / .wav / .brstm — audio replacement
    data/               offset-addressed data writes (data.toml or *.bin patches)
    models/             *.m3mp model packs (advanced, see §4.4)
    patches/            Riivolution-style XML file replacements
    package.toml        optional bundle manifest (multiple mods in one archive)
```

`mod_manager.cpp`:

* **Discovery** — scans `mods/` once at startup (and on "refresh" in the UI),
  reads `mod.toml` with the already-vendored `toml11`, validates ids, sorts by
  user-defined order.
* **Activation** — `mods/enabled.txt` stores the active set. Loading is lazy,
  per subsystem, and idempotent (textures are installed in Aurora's replacement
  index *before* the first frame; audio and data are consumed on demand).
* **Texture mods** — adds the mod's `textures/` directory to the list Aurora
  scans. Aurora only accepts directories, so each texture mod gets a
  `graphics/texture_replacements/<order>_<mod-id>` symlink/copy directory...
  see §4.3 for the exact mechanism.
* **Audio mods** — a mod may ship a replacement for a `16/srt/*` stream. The
  runtime installs a `StdFileOverride`-like map consulted by
  `AssetCache::Read/extract` so that a replaced stream is served from the mod
  file instead of the disc. Original files are never written.
* **Data mods** — a declarative list of guest-memory writes applied once when
  the game's main loop starts (optionally re-applied every frame), using the
  same validated `Gecko::Write` façade. This covers weapon/armour stat
  modification without recompiling the DOL.
* **Model mods** — the model pipeline is a *converted pack* pipeline keyed by
  partition offset (`AssetCache::ModelPath`). A model mod supplies a converted
  `M3MP` pack for a given source offset; the manager registers an override map
  consulted in `EnsureNativeModelAssets`, so the pack that gets admitted comes
  from the mod. (Documented limitation: packs must be produced with the
  recompilation's own converter — the format is versioned and validated, which
  keeps the renderer safe.)
* **Riivolution-style file patches** — `patches/*.xml` with `xml` `file`
  elements pointing at a mod file and a disc file name; installed through
  `DiscImage::SetFileOverride`, preserving sizes (the existing contract for
  quest overlays).
* **Custom asset packages** — a `.mh3mod` archive (zip without compression,
  implemented in `mod_archive.cpp` to avoid new dependencies) containing any of
  the above; unpacked into the mods directory on import.

### 4.3 Texture replacement without touching the game files

Aurora's index is rebuilt from a set of directories. The manager therefore:

1. Creates `graphics/texture_replacements/` (already the automatic root).
2. For each active texture mod, records its directory in
   `graphics/mod_textures.txt`.
3. Because `MH3_TEXTURE_DIR` scans exactly one extra directory, the manager
   materialises a **directory of hard links** (falling back to copies on
   filesystems without links, e.g. FAT) at
   `graphics/texture_replacements/mods/` at activation time, and rebuilds it on
   every change. Filenames are unique per texture hash, so collisions between
   mods are impossible for the same asset — and when two active mods provide the
   same asset, the higher-priority mod wins deterministically and the UI shows a
   conflict warning.

No original file is read-modify-written anywhere: the disc image is only ever
read, and every override is a separate file under the user directory.

### 4.4 Weapon / armour modification

Two complementary mechanisms:

1. **Declarative data mods** (`data/*.toml`) — writes to the game's equipment
   tables at startup (attack, defence, slots, sharpness, element). This is the
   same surface the native cheats use, so it is verified to be stable and is
   save-safe (it does not change save data unless the player saves).
2. **Model/texture mods** — a weapon or armour *visual* mod is a texture mod
   plus (optionally) a model pack; the manager groups them under one mod id so
   the UI shows "Weapon: Iron Katana — HD" as a single row.

### 4.5 UI

`Mods` tab: list of installed mods with toggle switches, type badges
(TEX/MDL/AUD/DAT/XML), conflict and error indicators, an "Import .mh3mod"
action that opens the file browser, "Reveal folder", "Refresh", "Export list".
Individual activation/deactivation writes `enabled.txt` immediately and applies
the change live (textures: index rebuild; audio/data: next load/step).

---

## 5. Persistence, save compatibility and performance

* Save files are untouched: all mods and cheats are external. `CantSave`
  semantics are unchanged. Data-mod writes happen in RAM after load, exactly
  like the existing cheats.
* Cheat state is stored outside the save, in `$MH3_USER_DIR/cheats`.
* The Gecko engine runs only when at least one code is enabled (a cached flag),
  so the disabled path costs a single predictable branch per step.
* Category/search index is built once (static table), so the UI does no
  allocation per frame; row rendering is virtualised by scroll offset.
* Mod discovery is one directory walk at startup; texture index rebuild happens
  only when activation changes.

## 6. Build and CI

`.github/workflows/android.yml` has two jobs. **tests** builds the Gecko engine
and the cheat catalog tests with the runner's own compiler and runs them — no
disc, no guest memory, no Aurora, a couple of minutes. **apk** installs the
NDK, CMake and Ninja, builds `libmain.so` for arm64 with SDL3 and libpng vendored
from source, packages and signs with `packaging/android/build_apk.sh`, and
verifies the result (`apksigner`, and that `generated/` did not leak into the
APK).

The APK job needs `../generated` — 23,170 translated functions, ~630 MB,
produced from the disc and deliberately not in the repository — so it runs only
when the repository variable `MH3_GENERATED_URL` names an archive of it, and is
skipped (not failed) otherwise. Both scripts are environment-driven
(`ANDROID_HOME`, `ANDROID_NDK_HOME`, `MH3_SDL3_PREFIX`,
`MH3_LIBMAIN_SO`/`MH3_LIBPNG_SO`/`MH3_LIB_SDL3_SO`), so they no longer depend on
the author's machine layout.

Host tests are plain executables: `MH3GeckoTests` and `MH3CheatCatalogTests`
in `runtime/CMakeLists.txt` (built when `MH3_BUILD_HOST_TESTS` is on), and the
same two `g++` command lines in CI so they do not need the NDK.

## 7. Testing strategy

| Layer | How it is verified |
|---|---|
| Native cheat ids | `cheat_catalog_tests.cpp`: every cheat in the port's table has a row, no row is stale, every section has entries, the fallback rule classifies a cheat the table does not know. |
| Bundled library | `gecko_tests.cpp`: 261 entries, every data code runs against fake RAM and writes only through `po`. |
| Gecko parser | Table-driven tests: valid/invalid lines, comments, activators, end markers, region tags. |
| Gecko VM | Golden tests executing write/pointer/activator/if-else/repeat codes against a fake memory and asserting the resulting bytes. |
| Region translation | US→PAL delta tests with the 51 known port pairs. |
| Cheat config | Round-trip serialisation, unknown-name tolerance, import/export. |
| Mod manager | Manifest parsing, id validation, ordering, conflict detection, enable-set persistence, archive listing. |
| In-game | Manual checklist in `docs/cheats-and-mods-checklist.md` (toggle each of the requested cheats in a hunt, check save/reload, check texture and audio mods). |
