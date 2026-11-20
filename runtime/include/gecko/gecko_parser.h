#pragma once

#include "gecko/gecko_code.h"

#include <cstddef>
#include <string_view>

namespace Gecko {

struct ParseOptions {
    std::string_view targetGameId = "RMHP08";
    size_t maxTextBytes = 4u * 1024u * 1024u;
    size_t maxCodes = 4096;
    size_t maxInstructionsPerCode = 65536;
};

// Parses a Gecko text-code list. The parser does no file I/O and no memory
// access: it only validates the text and classifies each code's codetypes.
// A code whose text is structurally invalid is returned with `valid` false
// and an Error diagnostic; it is never silently dropped.
ParseResult Parse(std::string_view text, const ParseOptions& options = {});

} // namespace Gecko
