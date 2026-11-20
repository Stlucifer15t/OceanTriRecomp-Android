#pragma once

#include "gecko/gecko_code.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Gecko {

// The 7-bit Gecko codetype: the first byte of a code line shifted right by
// one. The comments give the usual two-digit first byte.
enum class Codetype : uint8_t {
    Write8 = 0x00,       // 00
    Write16 = 0x01,      // 02
    Write32 = 0x02,      // 04
    StringWrite = 0x03,  // 06
    Serial = 0x04,       // 08

    Write8Po = 0x08,      // 10
    Write16Po = 0x09,     // 12
    Write32Po = 0x0A,     // 14
    StringWritePo = 0x0B, // 16
    SerialPo = 0x0C,      // 18

    If32Equal = 0x10,    // 20
    If32NotEqual = 0x11, // 22
    If32Greater = 0x12,  // 24
    If32Lower = 0x13,    // 26
    If16Equal = 0x14,    // 28
    If16NotEqual = 0x15, // 2A
    If16Greater = 0x16,  // 2C
    If16Lower = 0x17,    // 2E

    If32EqualPo = 0x18,    // 30
    If32NotEqualPo = 0x19, // 32
    If32GreaterPo = 0x1A,  // 34
    If32LowerPo = 0x1B,    // 36
    If16EqualPo = 0x1C,    // 38
    If16NotEqualPo = 0x1D, // 3A
    If16GreaterPo = 0x1E,  // 3C
    If16LowerPo = 0x1F,    // 3E

    BaseLoad = 0x20,    // 40
    BaseSet = 0x21,     // 42
    BaseStore = 0x22,   // 44
    BaseNext = 0x23,    // 46
    PointerLoad = 0x24, // 48
    PointerSet = 0x25,  // 4A
    PointerStore = 0x26, // 4C
    PointerNext = 0x27,  // 4E

    BaseLoadPo = 0x28,    // 50
    BaseSetPo = 0x29,     // 52
    BaseStorePo = 0x2A,   // 54
    PointerLoadPo = 0x2C, // 58
    PointerSetPo = 0x2D,  // 5A
    PointerStorePo = 0x2E, // 5C

    RepeatSet = 0x30,     // 60
    RepeatExecute = 0x31, // 62
    Return = 0x32,        // 64
    Goto = 0x33,          // 66
    Gosub = 0x34,         // 68

    RegisterSet = 0x40,    // 80
    RegisterLoad = 0x41,   // 82
    RegisterStore = 0x42,  // 84
    RegisterOp = 0x43,     // 86
    RegisterOp2 = 0x44,    // 88
    MemoryCopy1 = 0x45,    // 8A
    MemoryCopy2 = 0x46,    // 8C

    RegisterSetPo = 0x48,   // 90
    RegisterLoadPo = 0x49,  // 92
    RegisterStorePo = 0x4A, // 94
    RegisterOpPo = 0x4B,    // 96
    RegisterOp2Po = 0x4C,   // 98
    MemoryCopy1Po = 0x4D,   // 9A
    MemoryCopy2Po = 0x4E,   // 9C

    RegisterIfEqual = 0x50,    // A0
    RegisterIfNotEqual = 0x51, // A2
    RegisterIfGreater = 0x52,  // A4
    RegisterIfLower = 0x53,    // A6
    CounterIfEqual = 0x54,     // A8
    CounterIfNotEqual = 0x55,  // AA
    CounterIfGreater = 0x56,   // AC
    CounterIfLower = 0x57,     // AE

    RegisterIfEqualPo = 0x58,    // B0
    RegisterIfNotEqualPo = 0x59, // B2
    RegisterIfGreaterPo = 0x5A,  // B4
    RegisterIfLowerPo = 0x5B,    // B6

    ExecuteAsm = 0x60,        // C0
    InsertAsm = 0x61,         // C2
    CreateBranch = 0x63,      // C6
    OnOffSwitch = 0x66,       // CC
    AddressRange = 0x67,      // CE
    InsertAsmPo = 0x69,       // D2
    CreateBranchPo = 0x6B,    // D6
    AddressRangePo = 0x6F,    // DE

    FullTerminator = 0x70, // E0
    EndIf = 0x71,          // E2

    EndOfCodes = 0x78,           // F0
    InsertAsmChecksum = 0x79,    // F2
    InsertAsmChecksumPo = 0x7A,  // F4
    SearchSetPointer = 0x7B,     // F6
};

struct CodetypeInfo {
    uint8_t type = 0;
    const char* name = "unknown";
    SupportLevel support = SupportLevel::Unknown;
    uint8_t lines = 1; // 0 when the length depends on the operands
    const char* note = "";
};

CodetypeInfo ClassifyCodetype(uint8_t type);

struct CodetypeLength {
    uint32_t lines = 0; // total code lines, each two 32-bit words
    bool valid = false;
    const char* error = nullptr;
};

// The number of text code lines a codetype occupies, including its first
// line. A line is two 32-bit words. Variable-length types (string writes,
// ASM blocks, searches) read their count from the operands.
CodetypeLength InstructionLength(uint8_t type, uint32_t addressWord, uint32_t valueWord);

// The worse of two statuses: Unsupported beats RecognizedNotImplemented,
// which beats Unknown, which beats Supported.
SupportLevel CombineSupport(SupportLevel a, SupportLevel b);

// The region letter in a Wii game ID (the fourth character): E is US, P is
// EU, J is JP, K is KR, W is TW. Empty when the ID is not six characters.
std::string RegionFromGameId(std::string_view gameId);

// Match: the same game ID. RegionMismatch: the same three-character game
// code with a different region letter (RMHE08 vs RMHP08). Unknown: no ID or
// a different game. Addresses are never rewritten.
GameCompatibility ClassifyGameCompatibility(std::string_view codeGameId, std::string_view targetGameId);

} // namespace Gecko
