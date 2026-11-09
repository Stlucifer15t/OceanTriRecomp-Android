#pragma once

// The category of every native cheat, for the manager's sections
// (docs/cheats-and-mods.md 2.1).
//
// The port's own table groups cheats by who wrote the original code
// ("Skiller", "Doudley", ...), which is history, not usefulness: a player
// looking for "no poison" scans 88 rows of Doudley's codes. The manager groups
// by what a cheat does instead, and this is the mapping.
//
// It is keyed by the cheat's *name*, not its index, so a cheat inserted in the
// port's table later keeps every other cheat's category, and a cheat this table
// does not know is classified by a keyword rule rather than dropped. A test
// asserts every entry of LauncherCheats::List() has a category, and that this
// table has no row naming a cheat that no longer exists.
#include <cstddef>
#include <span>
#include <string_view>

namespace CheatCatalog {

// The sections, in the order the manager shows them.
enum class Category : uint8_t {
    Player,      // health, stamina, statuses, resistances
    Items,       // consumables, ammo, the boxes, gathering
    Combat,      // damage, sharpness, monster HP, hit reactions
    Progression, // money, HR, quests, equipment, farm
    World,       // map, HUD, clock, weather, farcaster
    Movement,    // running, jumping, flying, evading
    Online,      // only meaningful connected
    Misc,        // galleries, language, the character creator
    Count,
};

constexpr size_t kCategoryCount = size_t(Category::Count);

// The category's name, as the manager's section header.
const char* Name(Category category);

// The category of the cheat with this name, by the table and then by the
// keyword rule. Every name gets one.
Category Of(std::string_view cheatName);

// Whether the table has a row for this cheat by name, as opposed to falling
// back to the keyword rule: the test uses it to prove the table covers the
// port's whole list and has no stale row.
bool HasRow(std::string_view cheatName);

// The number of rows, for the same check.
size_t RowCount();

// The section's index in the manager for this cheat, and the reverse.
size_t SectionOf(std::string_view cheatName);
std::span<const std::string_view> SectionNames();

} // namespace CheatCatalog
