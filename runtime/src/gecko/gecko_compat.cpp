#include "gecko/gecko_compat.h"

#include <algorithm>
#include <cctype>

namespace Gecko {
namespace {

constexpr uint32_t kMaxBlockLines = 1u << 16;

int SupportRank(SupportLevel level) {
    switch (level) {
    case SupportLevel::Supported:
        return 0;
    case SupportLevel::RecognizedNotImplemented:
        return 1;
    case SupportLevel::Unknown:
        return 2;
    case SupportLevel::Unsupported:
        return 3;
    }

    return 3;
}

bool EqualIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;

    return true;
}

} // namespace

const char* SupportLevelName(SupportLevel level) {
    switch (level) {
    case SupportLevel::Supported:
        return "supported";
    case SupportLevel::RecognizedNotImplemented:
        return "recognized, not implemented";
    case SupportLevel::Unsupported:
        return "unsupported";
    case SupportLevel::Unknown:
        return "unknown";
    }

    return "unknown";
}

const char* GameCompatibilityName(GameCompatibility compatibility) {
    switch (compatibility) {
    case GameCompatibility::Match:
        return "match";
    case GameCompatibility::RegionMismatch:
        return "region mismatch";
    case GameCompatibility::Unknown:
        return "unknown";
    }

    return "unknown";
}

const char* DiagnosticSeverityName(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::Error ? "error" : "warning";
}

CodetypeInfo ClassifyCodetype(uint8_t type) {
    switch (Codetype(type)) {
    case Codetype::Write8:
        return {type, "8-bit write & fill", SupportLevel::Supported, 1, "Writes a byte to N+1 consecutive addresses."};
    case Codetype::Write16:
        return {type, "16-bit write & fill", SupportLevel::Supported, 1, "Writes a halfword to N+1 consecutive addresses."};
    case Codetype::Write32:
        return {type, "32-bit write", SupportLevel::Supported, 1, "Writes a word to the address."};
    case Codetype::StringWrite:
        return {type, "string write", SupportLevel::Supported, 0, "Writes a byte string; the value word is the byte count."};
    case Codetype::Serial:
        return {type, "serial/multi write", SupportLevel::Supported, 2, "Writes a value to a series of addresses."};

    case Codetype::Write8Po:
        return {type, "8-bit write & fill (pointer)", SupportLevel::Supported, 1, "Pointer-relative 8-bit write."};
    case Codetype::Write16Po:
        return {type, "16-bit write & fill (pointer)", SupportLevel::Supported, 1, "Pointer-relative 16-bit write."};
    case Codetype::Write32Po:
        return {type, "32-bit write (pointer)", SupportLevel::Supported, 1, "Pointer-relative 32-bit write."};
    case Codetype::StringWritePo:
        return {type, "string write (pointer)", SupportLevel::Supported, 0, "Pointer-relative byte string."};
    case Codetype::SerialPo:
        return {type, "serial/multi write (pointer)", SupportLevel::Supported, 2, "Pointer-relative serial write."};

    case Codetype::If32Equal:
        return {type, "32-bit if equal", SupportLevel::Supported, 1, "Runs the following codes when the word matches."};
    case Codetype::If32NotEqual:
        return {type, "32-bit if not equal", SupportLevel::Supported, 1, "Runs the following codes when the word differs."};
    case Codetype::If32Greater:
        return {type, "32-bit if greater", SupportLevel::Supported, 1, "Runs the following codes when the word is greater."};
    case Codetype::If32Lower:
        return {type, "32-bit if lower", SupportLevel::Supported, 1, "Runs the following codes when the word is lower."};
    case Codetype::If16Equal:
        return {type, "16-bit if equal", SupportLevel::Supported, 1, "Masked 16-bit equality test."};
    case Codetype::If16NotEqual:
        return {type, "16-bit if not equal", SupportLevel::Supported, 1, "Masked 16-bit inequality test."};
    case Codetype::If16Greater:
        return {type, "16-bit if greater", SupportLevel::Supported, 1, "Masked 16-bit greater-than test."};
    case Codetype::If16Lower:
        return {type, "16-bit if lower", SupportLevel::Supported, 1, "Masked 16-bit less-than test."};

    case Codetype::If32EqualPo:
        return {type, "32-bit if equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative 32-bit equality test."};
    case Codetype::If32NotEqualPo:
        return {type, "32-bit if not equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative 32-bit inequality test."};
    case Codetype::If32GreaterPo:
        return {type, "32-bit if greater (pointer)", SupportLevel::Supported, 1, "Pointer-relative 32-bit greater-than test."};
    case Codetype::If32LowerPo:
        return {type, "32-bit if lower (pointer)", SupportLevel::Supported, 1, "Pointer-relative 32-bit less-than test."};
    case Codetype::If16EqualPo:
        return {type, "16-bit if equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative masked 16-bit equality test."};
    case Codetype::If16NotEqualPo:
        return {type, "16-bit if not equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative masked 16-bit inequality test."};
    case Codetype::If16GreaterPo:
        return {type, "16-bit if greater (pointer)", SupportLevel::Supported, 1, "Pointer-relative masked 16-bit greater-than test."};
    case Codetype::If16LowerPo:
        return {type, "16-bit if lower (pointer)", SupportLevel::Supported, 1, "Pointer-relative masked 16-bit less-than test."};

    case Codetype::BaseLoad:
        return {type, "base address load", SupportLevel::Supported, 1, "Loads the base address from memory."};
    case Codetype::BaseSet:
        return {type, "base address set", SupportLevel::Supported, 1, "Sets the base address."};
    case Codetype::BaseStore:
        return {type, "base address store", SupportLevel::Supported, 1, "Stores the base address to memory."};
    case Codetype::BaseNext:
        return {type, "base address next", SupportLevel::Supported, 1, "Sets the base address to the next code's location."};
    case Codetype::PointerLoad:
        return {type, "pointer address load", SupportLevel::Supported, 1, "Loads the pointer address from memory."};
    case Codetype::PointerSet:
        return {type, "pointer address set", SupportLevel::Supported, 1, "Sets the pointer address."};
    case Codetype::PointerStore:
        return {type, "pointer address store", SupportLevel::Supported, 1, "Stores the pointer address to memory."};
    case Codetype::PointerNext:
        return {type, "pointer address next", SupportLevel::Supported, 1, "Sets the pointer address to the next code's location."};

    case Codetype::BaseLoadPo:
        return {type, "base address load (pointer)", SupportLevel::Supported, 1, "Pointer-relative base address load."};
    case Codetype::BaseSetPo:
        return {type, "base address set (pointer)", SupportLevel::Supported, 1, "Pointer-relative base address set."};
    case Codetype::BaseStorePo:
        return {type, "base address store (pointer)", SupportLevel::Supported, 1, "Pointer-relative base address store."};
    case Codetype::PointerLoadPo:
        return {type, "pointer address load (pointer)", SupportLevel::Supported, 1, "Pointer-relative pointer load."};
    case Codetype::PointerSetPo:
        return {type, "pointer address set (pointer)", SupportLevel::Supported, 1, "Pointer-relative pointer set."};
    case Codetype::PointerStorePo:
        return {type, "pointer address store (pointer)", SupportLevel::Supported, 1, "Pointer-relative pointer store."};

    case Codetype::RepeatSet:
        return {type, "set repeat", SupportLevel::Supported, 1, "Stores a repeat count and the next code address."};
    case Codetype::RepeatExecute:
        return {type, "execute repeat", SupportLevel::Supported, 1, "Repeats the stored block while the count is positive."};
    case Codetype::Return:
        return {type, "return", SupportLevel::Supported, 1, "Returns to the stored code address."};
    case Codetype::Goto:
        return {type, "goto", SupportLevel::Supported, 1, "Jumps relative to the next code line."};
    case Codetype::Gosub:
        return {type, "gosub", SupportLevel::Supported, 1, "Calls a relative code block."};

    case Codetype::RegisterSet:
        return {type, "set gecko register", SupportLevel::Supported, 1, "Sets a Gecko register."};
    case Codetype::RegisterLoad:
        return {type, "load gecko register", SupportLevel::Supported, 1, "Loads a Gecko register from memory."};
    case Codetype::RegisterStore:
        return {type, "store gecko register", SupportLevel::Supported, 1, "Stores a Gecko register to memory."};
    case Codetype::RegisterOp:
        return {type, "gecko register/direct value op", SupportLevel::Supported, 1, "Arithmetic or bitwise operation on a Gecko register."};
    case Codetype::RegisterOp2:
        return {type, "gecko register op", SupportLevel::Supported, 1, "Operation between two Gecko registers."};
    case Codetype::MemoryCopy1:
        return {type, "memory copy 1", SupportLevel::Supported, 1, "Copies bytes from a Gecko register address."};
    case Codetype::MemoryCopy2:
        return {type, "memory copy 2", SupportLevel::Supported, 1, "Copies bytes to a Gecko register address."};

    case Codetype::RegisterSetPo:
        return {type, "set gecko register (pointer)", SupportLevel::Supported, 1, "Pointer-relative Gecko register set."};
    case Codetype::RegisterLoadPo:
        return {type, "load gecko register (pointer)", SupportLevel::Supported, 1, "Pointer-relative Gecko register load."};
    case Codetype::RegisterStorePo:
        return {type, "store gecko register (pointer)", SupportLevel::Supported, 1, "Pointer-relative Gecko register store."};
    case Codetype::RegisterOpPo:
        return {type, "gecko register/direct value op (pointer)", SupportLevel::Supported, 1, "Pointer-relative Gecko register operation."};
    case Codetype::RegisterOp2Po:
        return {type, "gecko register op (pointer)", SupportLevel::Supported, 1, "Pointer-relative operation between two Gecko registers."};
    case Codetype::MemoryCopy1Po:
        return {type, "memory copy 1 (pointer)", SupportLevel::Supported, 1, "Pointer-relative memory copy."};
    case Codetype::MemoryCopy2Po:
        return {type, "memory copy 2 (pointer)", SupportLevel::Supported, 1, "Pointer-relative memory copy."};

    case Codetype::RegisterIfEqual:
        return {type, "gecko register if equal", SupportLevel::Supported, 1, "Compares two Gecko registers."};
    case Codetype::RegisterIfNotEqual:
        return {type, "gecko register if not equal", SupportLevel::Supported, 1, "Compares two Gecko registers."};
    case Codetype::RegisterIfGreater:
        return {type, "gecko register if greater", SupportLevel::Supported, 1, "Compares two Gecko registers."};
    case Codetype::RegisterIfLower:
        return {type, "gecko register if lower", SupportLevel::Supported, 1, "Compares two Gecko registers."};
    case Codetype::CounterIfEqual:
        return {type, "counter if equal", SupportLevel::Supported, 1, "Compares a per-code counter with a masked value."};
    case Codetype::CounterIfNotEqual:
        return {type, "counter if not equal", SupportLevel::Supported, 1, "Compares a per-code counter with a masked value."};
    case Codetype::CounterIfGreater:
        return {type, "counter if greater", SupportLevel::Supported, 1, "Compares a per-code counter with a masked value."};
    case Codetype::CounterIfLower:
        return {type, "counter if lower", SupportLevel::Supported, 1, "Compares a per-code counter with a masked value."};

    case Codetype::RegisterIfEqualPo:
        return {type, "gecko register if equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative register comparison."};
    case Codetype::RegisterIfNotEqualPo:
        return {type, "gecko register if not equal (pointer)", SupportLevel::Supported, 1, "Pointer-relative register comparison."};
    case Codetype::RegisterIfGreaterPo:
        return {type, "gecko register if greater (pointer)", SupportLevel::Supported, 1, "Pointer-relative register comparison."};
    case Codetype::RegisterIfLowerPo:
        return {type, "gecko register if lower (pointer)", SupportLevel::Supported, 1, "Pointer-relative register comparison."};

    case Codetype::ExecuteAsm:
        return {type, "execute ASM", SupportLevel::Unsupported, 0, "Runs PowerPC instructions; the AOT runtime has no PPC interpreter."};
    case Codetype::InsertAsm:
        return {type, "insert ASM", SupportLevel::Unsupported, 0, "Patches executable code; translated native code is not affected."};
    case Codetype::CreateBranch:
        return {type, "create branch", SupportLevel::Unsupported, 1, "Writes a PowerPC branch into code; inert in the AOT runtime."};
    case Codetype::OnOffSwitch:
        return {type, "on/off switch", SupportLevel::Supported, 1, "Toggles the execution status; requires an if before it."};
    case Codetype::AddressRange:
        return {type, "address range check", SupportLevel::Supported, 1, "Checks the base address against a range."};
    case Codetype::InsertAsmPo:
        return {type, "insert ASM (pointer)", SupportLevel::Unsupported, 0, "Pointer-relative executable-code patch."};
    case Codetype::CreateBranchPo:
        return {type, "create branch (pointer)", SupportLevel::Unsupported, 1, "Pointer-relative PowerPC branch."};
    case Codetype::AddressRangePo:
        return {type, "address range check (pointer)", SupportLevel::Supported, 1, "Pointer-relative address range check."};

    case Codetype::FullTerminator:
        return {type, "full terminator", SupportLevel::Supported, 1, "Clears the execution status and resets ba/po."};
    case Codetype::EndIf:
        return {type, "endif (+else)", SupportLevel::Supported, 1, "Ends one or more if blocks."};

    case Codetype::EndOfCodes:
        return {type, "end of codes", SupportLevel::Supported, 1, "Stops the code list."};
    case Codetype::InsertAsmChecksum:
        return {type, "insert ASM + checksum", SupportLevel::Unsupported, 0, "Executable-code patch with a checksum."};
    case Codetype::InsertAsmChecksumPo:
        return {type, "insert ASM + checksum (pointer)", SupportLevel::Unsupported, 0, "Pointer-relative executable-code patch."};
    case Codetype::SearchSetPointer:
        return {type, "search, set pointer", SupportLevel::Supported, 0, "Memory search; implemented with a bounded range in the interpreter."};
    }

    return {type, "unknown", SupportLevel::Unknown, 1, "Not a documented Gecko codetype."};
}

CodetypeLength InstructionLength(uint8_t type, uint32_t addressWord, uint32_t valueWord) {
    switch (Codetype(type)) {
    case Codetype::StringWrite:
    case Codetype::StringWritePo: {
        const uint32_t bytes = valueWord;
        const uint32_t dataLines = (bytes + 7u) / 8u;

        if (dataLines > kMaxBlockLines)
            return {0, false, "string write is too large"};

        return {1 + dataLines, true, nullptr};
    }

    case Codetype::Serial:
    case Codetype::SerialPo:
        return {2, true, nullptr};

    case Codetype::ExecuteAsm:
    case Codetype::InsertAsm:
    case Codetype::InsertAsmPo: {
        const uint32_t lines = valueWord;

        if (lines > kMaxBlockLines)
            return {0, false, "ASM block is too large"};

        return {1 + lines + 1, true, nullptr};
    }

    case Codetype::InsertAsmChecksum:
    case Codetype::InsertAsmChecksumPo: {
        const uint32_t lines = valueWord & 0xFFu;
        return {1 + lines + 1, true, nullptr};
    }

    case Codetype::SearchSetPointer: {
        const uint32_t lines = addressWord & 0xFFu;
        return {1 + lines, true, nullptr};
    }

    default:
        return {1, true, nullptr};
    }
}

SupportLevel CombineSupport(SupportLevel a, SupportLevel b) {
    return SupportRank(a) >= SupportRank(b) ? a : b;
}

std::string RegionFromGameId(std::string_view gameId) {
    if (gameId.size() != 6)
        return {};

    switch (std::toupper(static_cast<unsigned char>(gameId[3]))) {
    case 'E':
        return "US";
    case 'P':
        return "EU";
    case 'J':
        return "JP";
    case 'K':
        return "KR";
    case 'W':
        return "TW";
    default:
        return "Unknown";
    }
}

GameCompatibility ClassifyGameCompatibility(std::string_view codeGameId, std::string_view targetGameId) {
    if (codeGameId.empty() || targetGameId.empty())
        return GameCompatibility::Unknown;

    if (EqualIgnoreCase(codeGameId, targetGameId))
        return GameCompatibility::Match;

    if (codeGameId.size() == 6 && targetGameId.size() == 6 &&
        EqualIgnoreCase(codeGameId.substr(0, 3), targetGameId.substr(0, 3)) &&
        std::toupper(static_cast<unsigned char>(codeGameId[3])) != std::toupper(static_cast<unsigned char>(targetGameId[3])))
        return GameCompatibility::RegionMismatch;

    return GameCompatibility::Unknown;
}

} // namespace Gecko
