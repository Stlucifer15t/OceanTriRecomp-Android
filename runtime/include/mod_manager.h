#pragma once

// The Mod Manager: the modifications installed beside the game, offline.
//
// A mod is a directory under $MH3_USER_DIR/mods (or the same layout under the
// disc's own "mods" directory) with a mod.ini:
//
//   [mod]
//   name   = HD Skins
//   author = someone
//   version = 1.2
//   order  = 10          ; lower installs first; a later mod wins a conflict
//   game   = RMHP08      ; optional: only this disc, or blank for both
//   enabled = 1
//
//   [files]
//   ; disc path = file inside the mod, relative to the mod's directory.
//   ; The disc file's length must not change: the game's FST knows it.
//   native/tex.bin = data/tex.bin
//   ; A "replace" of the same length also covers patching a file's bytes.
//
// Nothing here writes to the disc. The overrides are handed to the disc image
// (DiscImage::SetFileOverride), which keeps the disc's own bytes on disk and
// serves the modded ones from memory for this launch.
//
// The parts of a mod that are not disc files (texture packs, model packs) are
// read from the same directory by the asset layer; this manager only tracks
// them so the UI can show and switch them.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mods {

struct FileMapping {
    std::string discPath; // e.g. "15/m04d/m04a05d/m04a05d.bin"
    std::string modPath;  // relative to the mod's directory
};

struct Mod {
    std::string id;     // the directory's name
    std::string root;   // its absolute path
    std::string name;
    std::string author;
    std::string version;
    std::string game;    // an (optional) disc id the mod is for
    int order = 0;
    bool enabled = false;
    std::vector<FileMapping> files;
    // Why the mod cannot be installed (a missing file, a length that would
    // change, a game that does not match); empty when it can.
    std::string problem;

    // Set by Apply: how many of its files were handed to the disc image.
    size_t appliedFiles = 0;
};

// $MH3_USER_DIR/mods, created on first use.
std::string UserDirectory();

// Reads every mod's mod.ini and its enabled flag (mods/state.txt).
void Initialize();

// The mods, in install order (a lower `order` first, then by name).
const std::vector<Mod>& List();

// Enables or disables a mod; a mod with a `problem` cannot be enabled.
bool SetEnabled(size_t index, bool enabled);

// Saves the enabled flags.
void Save();

// Applies the enabled mods to the disc image: each mapped file is read, its
// length checked against the disc's, and handed over as an override. A mod
// that fails is disabled and reported in `errors`.
void Apply();

// The mods that were applied at the last Apply, for the UI.
size_t AppliedCount();

// The error of the last Apply, one line per mod that failed.
const std::vector<std::string>& Errors();

// Rescans the mods' directories (after the user drops a new one in).
void Rescan();

} // namespace Mods
