#pragma once

// The Gecko cheat manager: the code library (bundled, imported and custom
// codes), which of them are enabled, and the per-step execution the game runs.
//
// Nothing here writes to the disc: imported codes live in
// $MH3_USER_DIR/cheats/gecko.txt and only ever change guest RAM, and a write
// that would land in the loaded image is refused (gecko_engine.h).
#include "gecko_codes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Gecko {

enum class Source : uint8_t {
    Builtin, // from gecko_db.cpp, ships with the app
    Imported, // read from the user's gecko.txt
    Custom, // typed in the manager's editor
};

struct Entry {
    Code code;
    Source source = Source::Builtin;
    bool enabled = false;

    // The library's category (gecko_db.h), for the manager's sections: bundled
    // entries have one; imported ones take the file's "# group:" line.
    std::string group;

    // Empty when this runtime can run the code; else why it cannot, which the
    // manager shows and the step pass reports.
    std::string blocked;

    // The built-in cheat of runtime/src/cheats.cpp that implements the code's
    // effect when it cannot run as written (gecko_db.h).
    std::string native;

    // The code's lines with this disc's addresses, built when the entry is
    // added or its region changes; this is what runs.
    std::vector<Line> plan;
};

// $MH3_USER_DIR/cheats (default /storage/emulated/0/MH3/cheats), created on
// first use.
std::string UserDirectory();

// Loads the bundled library and the user's codes. Safe to call more than once:
// a second call reloads the user's part and keeps the enabled states by name.
void Initialize();

// The codes: the bundled library in its order, then the user's.
const std::vector<Entry>& Entries();

// Adds parsed codes; imported and custom ones are persisted. Returns how many.
size_t AddCodes(const std::vector<Code>& codes, Source source);

// Parses and adds a Gecko text block. `error` gets the first unparsable line.
size_t ImportText(std::string_view text, Source source, std::string& error);

// Turns a code on or off. A code this runtime cannot run is refused and
// `blocked` says why (an enabled code that cannot run would be a dead switch).
bool SetEnabled(size_t index, bool enabled);

void Remove(size_t index);

// Re-plans and re-validates every entry for the disc's region.
void ApplyRegion(Region source);

// The region the codes are translated to: this disc's.
Region TargetRegion();

// Writes the user's codes and their enabled states.
void Save();

// The per-logic-step pass, from Cheats::Step. Cheap when nothing is enabled.
void Step();

// Runs one code now, ignoring its activator.
bool RunOnce(size_t index, std::string& error);

// Files a code's last error, for the manager to show.
std::string LastError(size_t index);

size_t Count();
size_t EnabledCount();

// How many of the entries this runtime can run as code, and how many of those
// are native ports (gecko_db.h): the manager shows both, so a switch that only
// points at a native cheat is never mistaken for a running code.
size_t RunnableCount();
size_t NativeBackedCount();

// The bundled groups, in the library's order.
std::vector<std::string> Groups();

} // namespace Gecko
