// Gecko code text parsing, classification and region translation
// (gecko_codes.h). Pure: no guest memory and no I/O beyond the strings passed
// in, so the host tests exercise exactly the code the game runs.
#include "gecko_codes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Gecko {
namespace {

constexpr uint32_t kMem1Bytes = 0x01800000u;
constexpr uint32_t kMem2Physical = 0x10000000u;
constexpr uint32_t kMem2Bytes = 0x04000000u;

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return lower;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool HexWord(std::string_view text, uint32_t& value) {
    if (text.empty() || text.size() > 8)
        return false;

    uint32_t result = 0;

    for (char c : text) {
        const int digit = HexDigit(c);

        if (digit < 0)
            return false;

        result = (result << 4) | uint32_t(digit);
    }

    value = result;
    return true;
}

// One instruction line: 8 hex digits, a separator, 8 more; a trailing comment
// after '#', "//" or ';' is allowed.
bool ParseInstructionLine(std::string_view text, uint32_t& address, uint32_t& value) {
    for (const std::string_view marker : {"#", "//", ";"}) {
        const size_t at = text.find(marker);

        if (at != std::string_view::npos)
            text = Trim(text.substr(0, at));
    }

    size_t digits = 0;

    while (digits < text.size() && digits < 8 && HexDigit(text[digits]) >= 0)
        ++digits;

    if (digits == 0 || !HexWord(text.substr(0, digits), address))
        return false;

    std::string_view rest = Trim(text.substr(digits));

    if (!rest.empty() && (rest.front() == '-' || rest.front() == ':' || rest.front() == '+' || rest.front() == ','))
        rest = Trim(rest.substr(1));

    if (rest.size() < 8 || !HexWord(rest.substr(0, 8), value))
        return false;

    return Trim(rest.substr(8)).empty();
}

bool IsComment(std::string_view line) {
    if (line.empty())
        return true;

    const char first = line.front();
    return first == '#' || first == '*' || first == '/' || first == ';';
}

// A code's name: "$Name", or "Name = value" (the shape several published
// lists use). Empty when the line is neither.
std::string NameOf(std::string_view line) {
    line = Trim(line);

    if (!line.empty() && line.front() == '$')
        return std::string(Trim(line.substr(1)));

    const size_t equals = line.find('=');

    if (equals == std::string_view::npos || equals == 0)
        return {};

    const auto left = Trim(line.substr(0, equals));
    uint32_t ignored = 0;

    if (left.empty() || HexWord(left, ignored))
        return {};

    return std::string(left);
}

Region RegionOfComment(std::string_view line) {
    const std::string lower = Lower(line);
    const size_t at = lower.find("region");

    if (at == std::string::npos)
        return Region::Unknown;

    std::string_view tail(lower);
    tail.remove_prefix(at + 6);

    while (!tail.empty() && (tail.front() == ':' || tail.front() == '=' || std::isspace(static_cast<unsigned char>(tail.front()))))
        tail.remove_prefix(1);

    if (tail.starts_with("pal") || tail.starts_with("eur"))
        return Region::Pal;

    if (tail.starts_with("ntsc-u") || tail.starts_with("ntscu") || tail.starts_with("usa") || tail == "us" || tail.starts_with("us "))
        return Region::NtscU;

    if (tail.starts_with("ntsc-j") || tail.starts_with("ntscj") || tail.starts_with("jpn") || tail == "jp" || tail.starts_with("jp "))
        return Region::NtscJ;
    return Region::Unknown;
}

// Whether an address is guest RAM this runtime maps (MEM1, MEM2 and their
// aliases). Code addresses outside it can never be used.
bool InGuestRam(uint32_t address) {
    const uint32_t physical = address & 0x3FFFFFFF;
    return physical < kMem1Bytes || (physical >= kMem2Physical && physical < kMem2Physical + kMem2Bytes);
}

bool IsWriteOp(Op op) {
    return op == Op::Write8 || op == Op::Write16 || op == Op::Write32 || op == Op::WriteString || op == Op::WriteSerial;
}

bool IsConditionalOp(Op op) {
    return op >= Op::If32Equal && op <= Op::If16Less;
}

bool IsBaseOp(Op op) {
    return op >= Op::SetBase && op <= Op::SetPointerToCode;
}

} // namespace

const char* RegionName(Region region) {
    switch (region) {
    case Region::Pal:
        return "PAL";
    case Region::NtscU:
        return "NTSC-U";
    case Region::NtscJ:
        return "NTSC-J";
    default:
        return "unknown";
    }
}

std::optional<Region> ParseRegion(std::string_view name) {
    const std::string lower = Lower(Trim(name));

    if (lower.empty())
        return std::nullopt;

    if (lower == "pal" || lower == "eur" || lower == "eu" || lower == "rmhp08")
        return Region::Pal;

    if (lower == "us" || lower == "usa" || lower == "ntsc-u" || lower == "ntscu" || lower == "rmhe08")
        return Region::NtscU;

    if (lower == "jp" || lower == "jpn" || lower == "ntsc-j" || lower == "ntscj" || lower == "rmhj08")
        return Region::NtscJ;
    return std::nullopt;
}

Translation TranslateRegion(uint32_t address, Region from, Region to) {
    Translation out;
    out.address = address;

    if (from == Region::Unknown || to == Region::Unknown)
        return out; // an unmarked list is taken as this disc's own

    if (from == to)
        return out;

    if (from == Region::NtscJ || to == Region::NtscJ) {
        out.error = "no verified NTSC-J address translation";
        return out;
    }

    out.address = uint32_t(int64_t(address) + (from == Region::NtscU ? kUsToPalDelta : -kUsToPalDelta));
    out.translated = true;

    if (!InGuestRam(out.address)) {
        out.address = address;
        out.translated = false;
        out.error = "translated address leaves guest RAM";
    }

    return out;
}

const char* ClassificationName(Classification kind) {
    switch (kind) {
    case Classification::Data:
        return "data";
    case Classification::Conditional:
        return "conditional";
    case Classification::Pointer:
        return "pointer";
    case Classification::Asm:
        return "asm";
    case Classification::Empty:
        return "empty";
    default:
        return "invalid";
    }
}

Classification Classify(const Code& code) {
    // The words a multi-line operation carries (a string's bytes, a serial's
    // parameters, a C0 block's instructions) belong to that operation, not to
    // the list, so they are stepped over.
    const auto wordsOf = [](const std::vector<Line>& body, size_t index) -> size_t {
        const Op op = OpOf(body[index].address);

        switch (op) {
        case Op::WriteString: {
            // The bytes are the list's raw words, two per line.
            const size_t words = (size_t(body[index].value) + 3) / 4;
            return (words + 1) / 2;
        }
        case Op::WriteSerial:
            return 1;
        case Op::ExecuteCode:
        case Op::Hook:
        case Op::HookBranch:
            return body[index].value;
        default:
            return 0;
        }
    };

    bool data = false, conditional = false, pointer = false, asmCode = false, invalid = false;

    for (size_t index = 0; index < code.body.size(); ++index) {
        const Op op = OpOf(code.body[index].address);

        if (IsWriteOp(op)) {
            data = true;

            if (op == Op::WriteSerial)
                invalid = true;
        } else if (IsConditionalOp(op)) {
            conditional = true;
            data = true;
        } else if (op == Op::SetPointer || op == Op::LoadPointer) {
            pointer = true;
            data = true;
        } else if (IsBaseOp(op)) {
            data = true;
        } else if (op == Op::ExecuteCode) {
            asmCode = true;
        } else if (op == Op::Hook || op == Op::HookBranch) {
            asmCode = true;
        } else if (op == Op::Toggle || op == Op::RangeCheck) {
            conditional = true;
        } else if (op == Op::End || op == Op::EndIf) {
            // Terminators carry no action of their own.
        } else {
            invalid = true;
        }

        // Step over the words this operation carries.
        if (const size_t extra = wordsOf(code.body, index))
            index = std::min(index + extra, code.body.size());
    }

    if (invalid)
        return Classification::Invalid;

    if (asmCode)
        return Classification::Asm;

    if (pointer)
        return Classification::Pointer;

    if (conditional)
        return Classification::Conditional;

    return data ? Classification::Data : Classification::Empty;
}

std::string UnsupportedReason(const Code& code) {
    if (code.body.empty())
        return "the code has no instructions";

    for (size_t index = 0; index < code.body.size(); ++index) {
        const Op op = OpOf(code.body[index].address);
        size_t extra = 0;

        switch (op) {
        case Op::Hook:
        case Op::HookBranch:
            return "hooks PowerPC code into a game function; this disc is statically translated, so the cheat needs a native port (the bundled library lists the original under the same name)";
        case Op::ExecuteCode:
            extra = size_t(code.body[index].value);
            break;
        case Op::WriteString:
            extra = ((size_t(code.body[index].value) + 3) / 4 + 1) / 2;
            break;
        case Op::WriteSerial:
            extra = 1;
            break;
        case Op::RegisterOp:
        case Op::LoadRegister:
        case Op::StoreRegister:
        case Op::ArithmeticRegister:
        case Op::ArithmeticTwo:
        case Op::CopyMemory:
            return "uses the Gecko code handler's own registers, which need that handler";
        case Op::SetRepeat:
        case Op::ExecuteRepeat:
        case Op::Return:
        case Op::Goto:
        case Op::Gosub:
            return "uses the Gecko code handler's repeat/goto control flow, which is not implemented";
        case Op::SetBaseToCode:
        case Op::SetPointerToCode:
            return "sets a base to the code's own location, which has no meaning here";
        case Op::RangeCheck:
            return "runs only at an address range, which needs the code handler's state";
        default:
            break;
        }

        if (extra)
            index = std::min(index + extra, code.body.size());
    }

    return {};
}

ParsedList Parse(std::string_view text) {
    ParsedList out;
    Code current;
    bool haveCode = false;
    size_t lineNumber = 0;

    const auto flush = [&] {
        if (!haveCode)
            return;

        if (current.body.empty() && !current.activator) {
            haveCode = false;
            current = Code{};
            return;
        }

        current.sourceRegion = out.declaredRegion;
        out.codes.push_back(std::move(current));
        current = Code{};
        haveCode = false;
    };

    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view raw = end == std::string_view::npos ? text : text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        ++lineNumber;

        const std::string_view line = Trim(raw);

        if (line.empty())
            continue;

        if (const Region region = RegionOfComment(line); region != Region::Unknown) {
            flush();
            out.declaredRegion = region;
            continue;
        }

        if (IsComment(line))
            continue;

        if (const std::string name = NameOf(line); !name.empty()) {
            flush();
            current.name = name;
            haveCode = true;
            continue;
        }

        Line parsed;

        if (!ParseInstructionLine(line, parsed.address, parsed.value)) {
            out.errors.push_back({lineNumber, "not a Gecko instruction: " + std::string(line)});
            continue;
        }

        haveCode = true;

        // A 16-bit if on nothing (address 0) is how a code list marks its
        // activator: 28000000 MMMMXXXX - the buttons in XXXX, masked by MMMM,
        // held. It must be the code's first line to count as one.
        const Op op = OpOf(parsed.address);
        const bool activatorShape = op == Op::If16Equal && (parsed.address & 0x01FFFFFEu) == 0;

        if (activatorShape && !current.activator && current.body.empty()) {
            current.activator = parsed;
            continue;
        }

        current.body.push_back(parsed);

        if (op == Op::End) {
            current.body.pop_back();
            flush();
        }
    }

    flush();
    return out;
}

std::string Format(const Code& code) {
    std::string out = "$";
    out += code.name.empty() ? "code" : code.name;
    out += '\n';

    if (code.sourceRegion != Region::Unknown) {
        out += "#region ";
        out += RegionName(code.sourceRegion);
        out += '\n';
    }

    char line[40];

    if (code.activator) {
        std::snprintf(line, sizeof(line), "%08X %08X\n", code.activator->address, code.activator->value);
        out += line;
    }

    for (const Line& l : code.body) {
        std::snprintf(line, sizeof(line), "%08X %08X\n", l.address, l.value);
        out += line;
    }

    return out;
}

std::string FormatAll(const std::vector<Code>& codes, Region region) {
    std::string out = "# Monster Hunter Tri Gecko codes\n#region ";
    out += RegionName(region);
    out += "\n\n";

    for (const Code& code : codes) {
        out += Format(code);
        out += '\n';
    }

    return out;
}

} // namespace Gecko
