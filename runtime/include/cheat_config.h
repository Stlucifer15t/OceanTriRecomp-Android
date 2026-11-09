#pragma once

// Saving the player's cheat choices.
//
// The launcher's native cheats (runtime/src/cheats.cpp) live in process memory
// for one launch; this keeps them across launches, in
// $MH3_USER_DIR/cheats/native.txt:
//
//   MH3CHEATS 1
//   CHEAT <index> 1 <choice> <text>
//
// The index is the cheat's position in LauncherCheats::List(), which the code
// fixes at compile time; loading checks the name as well, so a table that grows
// an entry in the middle cannot switch on a different cheat.
#include <string>

namespace CheatConfig {

// $MH3_USER_DIR/cheats (created on first use).
std::string UserDirectory();

// The file the choices are kept in.
std::string FilePath();

// Applies the saved choices; unknown or mismatched rows are ignored.
void Load();

// Writes every cheat's state.
void Save();

// Writes the state of one cheat (after a toggle, so a crash keeps it).
void SaveOne(size_t index);

} // namespace CheatConfig
