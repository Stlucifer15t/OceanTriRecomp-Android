#pragma once

// Gecko cheat codes: the text format the community publishes, the address
// policy of this disc, and the code types this runtime can execute.
//
// The instruction encoding is Gecko's published one: the address word's top
// byte is the operation, the next three bytes an address (or a count), and the
// data word the value. Nothing in this file touches guest memory: parsing is
// pure and testable on its own.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gecko {

// The retail discs of Monster Hunter Tri. Only the PAL one is translated by
// this repository; the others are accepted as the source of an imported list,
// whose addresses are then translated (see TranslateRegion).
enum class Region : uint8_t {
    Pal, // RMHP08, this disc
    NtscU, // RMHE08, the disc the published lists are written for
    NtscJ, // RMHJ08
    Unknown,
};

const char* RegionName(Region region);
std::optional<Region> ParseRegion(std::string_view name);

// US -> PAL delta of the code addresses, verified against the port table of
// runtime/src/cheat_*.cpp (52 address pairs, all -0x64).
constexpr int32_t kUsToPalDelta = -0x64;

struct Translation {
    uint32_t address = 0;
    bool translated = false; // the delta was applied
    const char* error = nullptr; // set when the address must not be used
};

// Translates a code address from `from` to `to`. Refuses NTSC-J (no verified
// delta for this title) and any result that leaves guest RAM.
Translation TranslateRegion(uint32_t address, Region from, Region to);

// One parsed instruction line: the address word and the data word.
struct Line {
    uint32_t address = 0;
    uint32_t value = 0;
};

// Gecko's operation, from the address word's top byte.
enum class Op : uint8_t {
    Write8 = 0x00,
    Write16 = 0x02,
    Write32 = 0x04,
    WriteString = 0x06,
    WriteSerial = 0x08,

    If32Equal = 0x20,
    If32NotEqual = 0x22,
    If32Greater = 0x24,
    If32Less = 0x26,
    If16Equal = 0x28,
    If16NotEqual = 0x2A,
    If16Greater = 0x2C,
    If16Less = 0x2E,

    SetBase = 0x40,
    SetBaseOffset = 0x42,
    StoreBase = 0x44,
    SetBaseToCode = 0x46,
    LoadPointer = 0x48,
    SetPointer = 0x4A,
    StorePointer = 0x4C,
    SetPointerToCode = 0x4E,

    SetRepeat = 0x60,
    ExecuteRepeat = 0x62,
    Return = 0x64,
    Goto = 0x66,
    Gosub = 0x68,

    RegisterOp = 0x80,
    LoadRegister = 0x82,
    StoreRegister = 0x84,
    ArithmeticRegister = 0x86,
    ArithmeticTwo = 0x88,
    CopyMemory = 0x8A,

    ExecuteCode = 0xC0,
    Hook = 0xC2,
    HookBranch = 0xC6,
    Toggle = 0xCC,
    RangeCheck = 0xCE,

    EndIf = 0xE0,
    End = 0xF0,
};

// The top byte of a line decodes as it does in Gecko's code handler
// (docs/codehandler.s of the Gecko OS handler):
//   type  = bits 0-2 of the byte   (0 write, 1 if, 2 base/pointer, 3 flow,
//                                   4 gecko registers, 5 [rN] compare,
//                                   6 hook/execute, 7 terminator)
//   sub   = bits 4-6
//   pointer = bit 4 set: po is used as the address, else ba
// A few forms (0x05, C4) differ from their neighbours only in the address's
// high bit or the base used, so OpOf folds them onto one Op.
inline uint32_t TypeOf(uint32_t addressWord) {
    return addressWord >> 29;
}

inline uint32_t SubOf(uint32_t addressWord) {
    return (addressWord >> 25) & 7;
}

inline bool UsesPointer(uint32_t addressWord) {
    return (addressWord & 0x10000000u) != 0;
}

inline Op OpOf(uint32_t addressWord) {
    switch (TypeOf(addressWord)) {
    case 0:
        return Op(0x00 + 2 * SubOf(addressWord));
    case 1:
        return Op(0x20 + 2 * SubOf(addressWord));
    case 2:
        return Op(0x40 + 2 * SubOf(addressWord));
    case 3:
        return Op(0x60 + 2 * SubOf(addressWord));
    case 4:
        return Op(0x80 + 2 * SubOf(addressWord));
    case 5:
        return Op(0xA0 + 2 * SubOf(addressWord));
    case 6:
        switch (SubOf(addressWord)) {
        case 0:
            return Op::ExecuteCode;
        case 3:
            return Op::HookBranch;
        case 6:
            return Op::Toggle;
        case 7:
            return Op::RangeCheck;
        default:
            return Op::Hook; // C2, C4 (and the unused C8/CA)
        }
    default:
        return UsesPointer(addressWord) ? Op::End : Op(0xE0 + 2 * SubOf(addressWord));
    }
}

// The 24-bit address (or count) field of a line.
inline uint32_t Address24(uint32_t addressWord) {
    return addressWord & 0x01FFFFFFu;
}

// The 25-bit form the handler computes: the address keeps its high bit, which
// the code type rides on.
inline uint32_t AddressField(uint32_t addressWord) {
    return addressWord & 0x01FFFFFFu;
}

// The 16-bit field some forms carry in the address (the high half of a 16-bit
// condition's value, a count of writes).
inline uint32_t Address16(uint32_t addressWord) {
    return addressWord & 0x0000FFFFu;
}

struct Error {
    size_t line = 0; // 1-based
    std::string message;
};

// One code: a name, an optional activator line, and its body.
struct Code {
    std::string name;
    Region sourceRegion = Region::Unknown;
    // A "28XXXXXX MMMMXXXX" activator line: while the buttons in MMMM are all
    // held, the body runs (Gecko's 16-bit conditional on the pad, used as a
    // trigger).
    std::optional<Line> activator;
    std::vector<Line> body;
    std::string note; // what the importer or the DB says about this code
};

// What running the code needs, which decides whether this runtime can do it.
enum class Classification : uint8_t {
    Data, // guest data memory only: runs as written
    Conditional, // data writes guarded by a memory comparison
    Pointer, // chases pointers through the base register: runs as written
    Asm, // injects PowerPC code: needs a native port instead
    Empty,
    Invalid, // an instruction this runtime does not implement
};

const char* ClassificationName(Classification kind);
Classification Classify(const Code& code);

// Why a code cannot run as written (empty when it can).
std::string UnsupportedReason(const Code& code);

struct ParsedList {
    std::vector<Code> codes;
    std::vector<Error> errors;
    Region declaredRegion = Region::Unknown;
};

// Extracts the codes of a Gecko list. Accepts the shapes the community
// publishes: "$Name" headers, "*comment" and "#comment" lines, "name = value"
// pairs, bare 8+8 hex with any separator, and "#region us" markers. A line
// that does not parse is reported with its number and skipped, so an imported
// file shows exactly where it went wrong.
ParsedList Parse(std::string_view text);

// Canonical text for the editor and for exports.
std::string Format(const Code& code);
std::string FormatAll(const std::vector<Code>& codes, Region region);

} // namespace Gecko
