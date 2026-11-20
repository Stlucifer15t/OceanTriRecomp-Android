#pragma once

#include "gecko/gecko_code.h"

#include <string>
#include <vector>

namespace Gecko {

// Ocean's user-data directory (MH3_USER_DIR) and the Gecko subdirectories.
std::string UserDir();
std::string GeckoDir();
std::string CodesPath();
std::string ImportsDir();

// Loads a Gecko text file. Missing files are not an error. Malformed files
// return the codes that did parse plus diagnostics; the original is copied to
// codes.txt.bak (or renamed to codes.txt.corrupt when nothing parsed).
bool LoadCodes(const std::string& path, std::vector<Code>& codes, std::vector<Diagnostic>& diagnostics);

// Writes the collection atomically (codes.txt.tmp then rename).
bool SaveCodes(const std::string& path, const std::vector<Code>& codes);

// Keeps the original imported text under gecko/imports/<sanitized name>.txt.
bool SaveImportedFile(const std::string& name, const std::string& text);

} // namespace Gecko
