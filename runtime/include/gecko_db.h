#pragma once

// The bundled Gecko code library: the codes the app ships with, available
// offline.
//
// Every entry is one of two things:
//   - a data code this runtime executes: writes to guest RAM, at the same
//     addresses and with the same values the native port of the same cheat
//     writes, so the code's effect is the port's;
//   - a cheat whose original code injects PowerPC instructions, which a static
//     recompilation cannot run. Its `body` is empty and `native` names the
//     built-in cheat of runtime/src/cheats.cpp that implements it instead.
//
// `native` may also cross-reference a data code ("the same change, done by the
// port, which follows a pointer where the code cannot"), so it is a hint, not a
// marker: an entry with a body runs.
//
// `body` is in Gecko's own encoding (gecko_codes.h), so the manager can show,
// edit and export the code like any imported one.
#include "gecko_codes.h"

#include <span>
#include <string>

namespace Gecko {

struct DbCode {
    const char* name;
    const char* group; // the author/category tag the list came from
    std::vector<Line> body;
    // A built-in cheat of runtime/src/cheats.cpp that does the same thing; set
    // for codes a static recompilation cannot execute as written.
    const char* native;
    const char* note;
};

std::span<const DbCode> DbCodes();

} // namespace Gecko
