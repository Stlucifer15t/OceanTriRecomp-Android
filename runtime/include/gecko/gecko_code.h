#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Custom Gecko cheat codes: the parsed representation shared by the parser,
// the compatibility table and (from Phase 1) the interpreter. Phase 0 does
// not execute anything: a code is data plus diagnostics.
namespace Gecko {

// Whether the planned interpreter can run a codetype.
//   Supported                 the interpreter will implement it;
//   RecognizedNotImplemented  a documented codetype we do not plan to run yet;
//   Unsupported               needs PPC assembly or translated-code changes;
//   Unknown                   not a documented Gecko codetype, or invalid.
enum class SupportLevel : uint8_t {
    Supported,
    RecognizedNotImplemented,
    Unsupported,
    Unknown,
};

// Whether a code's game ID matches the disc this build targets (RMHP08).
enum class GameCompatibility : uint8_t {
    Match,
    RegionMismatch,
    Unknown,
};

enum class DiagnosticSeverity : uint8_t {
    Warning,
    Error,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    int line = 0; // 1-based source line; 0 for a file-level diagnostic
    std::string codeName;
    std::string message;
};

// One 32-bit Gecko instruction pair. `addressWord` is the first word (the
// codetype and address); `valueWord` is the second. `codetype` is the 7-bit
// type (the top seven bits of the first byte) and `address` is the 25-bit
// address (the low bit of the first byte is address bit 24).
struct Instruction {
    uint32_t addressWord = 0;
    uint32_t valueWord = 0;
    uint8_t codetype = 0;
    uint32_t address = 0;
    // The words of the additional lines a multi-line codetype occupies
    // (string data, serial operands, ASM lines, search values), in order.
    std::vector<uint32_t> extraWords;
};

struct Code {
    uint64_t id = 0; // FNV-1a of the name and instruction words
    std::string name;
    std::string description; // the code's comments, joined by newlines
    std::vector<std::string> comments;
    std::string gameId;    // e.g. RMHE08, when the file names one
    std::string region;    // US, EU, JP, ... derived from gameId
    std::string sourceText; // the original text of this code block
    std::vector<Instruction> instructions;
    std::vector<Diagnostic> diagnostics;
    SupportLevel support = SupportLevel::Unknown;
    GameCompatibility gameCompatibility = GameCompatibility::Unknown;
    bool enabled = false;
    bool valid = false;
};

struct ParseResult {
    std::vector<Code> codes;
    std::vector<Diagnostic> diagnostics; // file-level diagnostics
    std::string headerText;              // text before the first $ code
    std::string gameId;
    std::string region;
};

const char* SupportLevelName(SupportLevel level);
const char* GameCompatibilityName(GameCompatibility compatibility);
const char* DiagnosticSeverityName(DiagnosticSeverity severity);

} // namespace Gecko
