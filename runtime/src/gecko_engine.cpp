// Executing Gecko codes (gecko_engine.h).
//
// The data operations follow Gecko's code handler exactly (dolphin-emu/dolphin
// docs/codehandler.s, MIT, Nuke/brkirch/Y.S/Kenobi/gamemasterplc), because the
// community's code lists are written against it:
//
//   00XXXXXX YYYY00ZZ   write ZZ to YYYY bytes
//   02XXXXXX YYYYZZZZ   write ZZZZ to YYYY halfwords
//   04XXXXXX ZZZZZZZZ   write a word
//   06XXXXXX 000000YY   write YY bytes, taken from the words that follow
//   08XXXXXX VVVVVVVV   serial write: TNNNZZZZ VVVVVVVV, one row per write
//   20..2E              if [data] compares; the block up to its E0 runs when true
//   40..4E              base address (ba) and pointer (po) arithmetic
//   60..68              repeat / goto / gosub
//   80..8C              the handler's own registers
//   A0..AF              [grN] against [grM]
//   C0                  execute the following lines as PowerPC code
//   C2/C6               hook PowerPC code into the game's own code
//   CC / CE             the handler's on-off switch and range check
//   E0/E2/F0            end of an if-block, else, end of the list
//
// The address a line acts on is `po` when the code's high bit 28 is set, else
// `ba`, plus the line's low 24 bits (the base/pointer forms put their operand
// in the data word instead; see Instruction).
//
// What this runtime runs: the writes, the conditionals, base/pointer
// arithmetic, C0 blocks and the terminators. Everything else is refused with
// the reason: repeat/goto and the register operations need the handler's own
// state, and C2/C6 need to execute PowerPC inside the game's code, which a
// static recompilation cannot do - the ARM64 functions were produced at build
// time. Every cheat that needs instructions exists as a native port instead
// (runtime/src/cheats.cpp).
#include "gecko_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Gecko {
namespace {

// An imported code costs one step at most when it is malformed or hostile:
// every loop is bounded, well above what a real code needs.
constexpr uint32_t kMaxWrites = 0x4000;
constexpr uint32_t kMaxSteps = 0x8000;

uint32_t Swap32(uint32_t value) {
    return __builtin_bswap32(value);
}

uint16_t Swap16(uint16_t value) {
    return uint16_t((value >> 8) | (value << 8));
}

std::string Hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    return text;
}

std::string HexByte(uint32_t value) {
    char text[8];
    std::snprintf(text, sizeof(text), "%02X", value);
    return text;
}

bool IsConditional(Op op) {
    return op >= Op::If32Equal && op <= Op::If16Less;
}

// The handler's own state: ba, po and the execution-status stack that ifs and
// the E0/E2 terminators push and pop.
struct Handler {
    Memory& memory;
    const std::vector<Line>& body;

    uint32_t base = 0x80000000u;
    uint32_t pointer = 0x80000000u;

    // Bit 0 is the innermost status: 0 = running, 1 = skipping. An if pushes,
    // its E0 pops.
    uint32_t status = 0;

    size_t writes = 0;
    uint32_t steps = 0;
    std::string error;

    bool Skipping() const { return (status & 1) != 0; }

    bool Fail(std::string message) {
        if (error.empty())
            error = std::move(message);
        return false;
    }

    uint32_t AddressOf(const Line& line) const {
        return (UsesPointer(line.address) ? pointer : base) + (line.address & 0x01FFFFFFu);
    }

    bool Store(uint32_t address, const void* bytes, size_t count) {
        if (count == 0)
            return true;

        if (!memory.InRam(address, count))
            return Fail("write outside guest RAM at 0x" + Hex(address));

        if (!memory.Writable(address, count))
            return Fail("write into the loaded image at 0x" + Hex(address));

        if (!memory.Write(address, bytes, count))
            return Fail("write failed at 0x" + Hex(address));

        ++writes;
        return true;
    }

    bool Store8(uint32_t address, uint8_t value) { return Store(address, &value, 1); }

    bool Store16(uint32_t address, uint16_t value) {
        const uint16_t be = Swap16(value);
        return Store(address & ~1u, &be, 2);
    }

    bool Store32(uint32_t address, uint32_t value) {
        const uint32_t be = Swap32(value);
        return Store(address & ~3u, &be, 4);
    }

    bool Load(uint32_t address, uint32_t& value, size_t bytes) {
        if (!memory.InRam(address, bytes))
            return Fail("read outside guest RAM at 0x" + Hex(address));

        switch (bytes) {
        case 1: {
            uint8_t v = 0;

            if (!memory.Read8(address, v))
                return Fail("read failed at 0x" + Hex(address));

            value = v;
            return true;
        }
        case 2: {
            uint16_t v = 0;

            if (!memory.Read16(address & ~1u, v))
                return Fail("read failed at 0x" + Hex(address));

            value = v;
            return true;
        }
        default: {
            uint32_t v = 0;

            if (!memory.Read32(address & ~3u, v))
                return Fail("read failed at 0x" + Hex(address));

            value = v;
            return true;
        }
        }
    }

    // 40/42/44/46 (ba) and 48/4A/4C/4E (po) share one operand layout in the
    // address word: bits 19-16 add ba/po (Y), bits 15-12 add grN (Z), bits 23-20
    // add rather than replace (T), bits 3-0 pick grN. This runtime has no grN
    // (the register operations need the handler), so Z must be 0.
    bool BasePointer(const Line& line, bool pointerForm);
    bool Instruction(size_t& index);
    bool Run();
};

bool Handler::BasePointer(const Line& line, bool pointerForm) {
    // 40/42/44/46 (ba) and 48/4A/4C/4E (po). The operand is the line's data
    // word, never its address: 4A000000 80796D40 sets po to 0x80796D40.
    //
    // The flags Gecko layers on top - T (add rather than replace), Y (add ba/po
    // to the operand), Z (add one of the handler's registers) - are refused
    // rather than guessed at: a wrong address in a save-data code is worse than
    // a clear "cannot run this".
    // Y is bit 15, Z bit 11, T bit 19 and N bits 23-20 of the address word
    // (the handler rotates the word to read them). The operand itself is the
    // data word, which is why those bits can be flags at all.
    //
    // Bit 27 - the pointer flag of 0x10/0x12/0x14 - is NOT a base/pointer flag:
    // these opcodes have their po forms as separate opcodes (48/4A/4C/4E), and
    // 0x08000000 bit 27 lies inside N, which is zero in theirs. The mask below
    // therefore excludes it, so 4A000000 (a plain "po = operand") is accepted;
    // the data opcodes' 14XXXXXX form is read the same way as their 04XXXXXX
    // form, with bit 27 selecting po instead of ba (AddressOf).
    if (line.address & 0x0088F800u)
        return Fail("uses the base/pointer operand flags, which are not implemented");

    const uint32_t operand = line.value;
    const Op op = OpOf(line.address);

    switch (op) {
    case Op::SetBase: // 40: ba = [operand]
    case Op::LoadPointer: { // 48: po = [operand]
        uint32_t loaded = 0;

        if (!Load(operand, loaded, 4))
            return false;

        if (pointerForm)
            pointer = loaded;
        else
            base = loaded;

        return true;
    }

    case Op::SetBaseOffset: // 42: ba = operand
        base = operand;
        return true;

    case Op::SetPointer: // 4A: po = operand
        pointer = operand;
        return true;

    case Op::StoreBase: // 44: [operand] = ba
        return Store32(operand, base);

    case Op::StorePointer: // 4C: [operand] = po
        return Store32(operand, pointer);

    default:
        return Fail("this base/pointer form is not implemented");
    }
}

bool Handler::Instruction(size_t& index) {
    const Line line = body[index];
    const Op op = OpOf(line.address);
    const uint32_t address = AddressOf(line);

    switch (op) {
    case Op::Write8: {
        // 00XXXXXX YYYY00ZZ: YYYY+1 bytes of ZZ (the handler writes, then
        // decrements and loops while not below zero).
        const uint32_t count = std::min<uint32_t>((line.value >> 16) + 1, kMaxWrites);

        for (uint32_t i = 0; i < count; ++i)
            if (!Store8(address + i, uint8_t(line.value)))
                return false;

        return true;
    }

    case Op::Write16: {
        const uint32_t count = std::min<uint32_t>((line.value >> 16) + 1, kMaxWrites);

        for (uint32_t i = 0; i < count; ++i)
            if (!Store16(address + i * 2, uint16_t(line.value)))
                return false;

        return true;
    }

    case Op::Write32:
        return Store32(address, line.value);

    case Op::WriteSerial: {
        // 08XXXXXX VVVVVVVV, then TNNNZZZZ VVVVVVVV: T is the width (0 byte,
        // 1 halfword, 2 word), NNN the further writes, ZZZZ the address step and
        // the second line's value the value's step.
        if (index + 1 >= body.size())
            return Fail("the serial write has no parameter line");

        const Line parameter = body[index + 1];
        const uint32_t width = parameter.address >> 28;
        const uint32_t rows = std::min<uint32_t>(((parameter.address >> 16) & 0x0FFFu) + 1, kMaxWrites);
        const uint32_t addressStep = parameter.address & 0xFFFFu;
        uint32_t value = line.value;

        for (uint32_t i = 0; i < rows; ++i, value += parameter.value) {
            if (width == 0) {
                if (!Store8(address + i * addressStep, uint8_t(value)))
                    return false;
            } else if (width == 1) {
                if (!Store16(address + i * addressStep, uint16_t(value)))
                    return false;
            } else if (!Store32(address + i * addressStep, value)) {
                return false;
            }
        }

        ++index; // the parameter line belongs to this write
        return true;
    }

    case Op::WriteString: {
        // 06XXXXXX YYYYYYYY, then the bytes as the code list's raw words: the
        // handler reads them straight out of the list with lbzx, so the bytes
        // run through each following line's address word and then its value
        // word. "HELLO" is 48454C4C 4F000000 on one line.
        const uint32_t count = std::min(line.value, kMaxWrites);
        const size_t words = (size_t(count) + 3) / 4;
        const size_t lines = (words + 1) / 2;

        if (index + lines >= body.size() && lines != 0)
            return Fail("the string write runs past the end of the code");

        for (uint32_t i = 0; i < count; ++i) {
            const size_t word = i / 4;
            const Line& source = body[index + 1 + word / 2];
            const uint32_t value = (word % 2 == 0) ? source.address : source.value;

            if (!Store8(address + i, uint8_t(value >> (24 - (i % 4) * 8))))
                return false;
        }

        index += lines;
        return true;
    }

    case Op::SetBase:
    case Op::SetBaseOffset:
        return BasePointer(line, false);

    case Op::LoadPointer:
        return BasePointer(line, true);

    case Op::StoreBase:
        return BasePointer(line, false);

    case Op::SetPointer:
        return BasePointer(line, true);

    case Op::StorePointer:
        return BasePointer(line, true);

    case Op::SetBaseToCode:
        return Fail("sets its base address to the code's own location, which has no meaning here");

    case Op::SetPointerToCode:
        return Fail("sets its pointer to the code's own location, which has no meaning here");

    case Op::ExecuteCode: {
        // C0: the data word is the number of PowerPC lines that follow; they
        // run here, in the handler's place, with a register file that starts at
        // zero. A block that reads a register it has not written would compute
        // from zeros where the Wii used the caller's values, so it is refused.
        const uint32_t count = std::min(line.value, kMaxWrites / 8);

        if (count == 0)
            return true;

        if (index + count >= body.size())
            return Fail("the code block runs past the end of the code");

        std::vector<uint32_t> words;
        words.reserve(count);

        for (uint32_t i = 0; i < count; ++i)
            words.push_back(body[index + 1 + i].address);

        std::string reason;

        if (!BlockRunnable(words, reason))
            return Fail(reason);

        const BlockResult result = ExecuteBlock(words, memory);

        if (!result.ok)
            return Fail(result.error);

        index += count;
        return true;
    }

    case Op::Hook:
    case Op::HookBranch:
        return Fail("hooks PowerPC code into the game's own functions, which a static recompilation cannot do");

    case Op::Toggle:
        // CC000000 0000000T: the handler flips its own on/off bit for the block
        // and its code list holds the state. That state is the manager's
        // enabled flag here, so the line itself does nothing.
        return true;

    case Op::RangeCheck:
        return Fail("runs the code only at an address range, which needs the handler's state");

    case Op::SetRepeat:
    case Op::ExecuteRepeat:
    case Op::Return:
    case Op::Goto:
    case Op::Gosub:
        return Fail("uses the code handler's repeat/goto control flow, which is not implemented");

    case Op::RegisterOp:
    case Op::LoadRegister:
    case Op::StoreRegister:
    case Op::ArithmeticRegister:
    case Op::ArithmeticTwo:
    case Op::CopyMemory:
        return Fail("uses the code handler's own registers, which need that handler");

    default:
        return Fail("unsupported Gecko operation 0x" + HexByte(line.address >> 24));
    }
}

bool Handler::Run() {
    size_t index = 0;

    while (index < body.size()) {
        if (++steps > kMaxSteps)
            return Fail("the code did not finish");

        const Line line = body[index];
        const Op op = OpOf(line.address);

        if (IsConditional(op)) {
            const bool sixteen = op >= Op::If16Equal;

            // Bit 0 of the address word means "pop an endif first" (that is
            // how a code adds an else).
            if (line.address & 1u) {
                if (status != 0)
                    status >>= 1;
            }

            if (!Skipping()) {
                // 16-bit ifs AND the data with the value's inverted high half.
                const uint32_t mask = sixteen ? uint32_t(uint16_t(~line.value >> 16)) : 0xFFFFFFFFu;
                const uint32_t compare = sixteen ? uint32_t(uint16_t(line.value)) : line.value;
                uint32_t data = 0;

                if (!Load(AddressOf(line), data, sixteen ? 2 : 4))
                    return false;

                data &= mask;

                // Sub 0 (and 4): equal, 1 (5): not equal, 2 (6): greater,
                // 3 (7): less, on an unsigned compare.
                bool holds;

                switch (SubOf(line.address) & 3) {
                case 0:
                    holds = data == compare;
                    break;
                case 1:
                    holds = data != compare;
                    break;
                case 2:
                    holds = data > compare;
                    break;
                default:
                    holds = data < compare;
                    break;
                }

                status = (status << 1) | (holds ? 0u : 1u);
            } else {
                status = (status << 1) | 1u; // a skipped if keeps its block skipped
            }

            ++index;
            continue;
        }

        if (op == Op::EndIf) {
            const uint32_t sub = SubOf(line.address);

            if (sub == 2)
                status = 0; // E2 with VV 2: an else at the top level
            else if (status != 0)
                status >>= 1;

            ++index;
            continue;
        }

        if (op == Op::End) {
            ++index;
            return true; // F0 ends the whole list
        }

        if (Skipping()) {
            ++index;
            continue;
        }

        if (!Instruction(index))
            return false;

        ++index;
    }

    return true;
}

// Gecko's button bits (the Classic Controller's, printed as a Wii code would
// write them), converted from this runtime's own Classic Controller bits
// (cheats.h Button).
uint16_t GeckoButtons(uint16_t held) {
    constexpr struct {
        uint16_t ours, gecko;
    } kMap[]{
        {0x0020, 0x0010}, // A
        {0x0040, 0x0040}, // B
        {0x0100, 0x0008}, // X
        {0x0200, 0x0020}, // Y
        {0x0008, 0x2000}, // L
        {0x0080, 0x0200}, // R
        {0x0004, 0x0080}, // ZL
        {0x0010, 0x0004}, // ZR
        {0x2000, 0x0001}, // Up
        {0x1000, 0x4000}, // Down
        {0x0800, 0x0002}, // Left
        {0x0400, 0x8000}, // Right
        {0x4000, 0x1000}, // Minus
        {0x8000, 0x0400}, // Plus
    };

    uint16_t out = 0;

    for (const auto& entry : kMap)
        if (held & entry.ours)
            out |= entry.gecko;

    return out;
}

// An activator line is Gecko's 16-bit if on the pad work (28XXXXXX MMMMXXXX):
// the buttons in the value's low half, masked by its high half, must match.
bool ActivatorHolds(const Line& activator, uint16_t held) {
    const uint16_t buttons = GeckoButtons(held);
    const uint16_t expected = uint16_t(activator.value & 0xFFFF);
    const uint16_t mask = uint16_t(~activator.value >> 16);

    if (expected == 0)
        return true; // no buttons wanted

    return (buttons & mask) == expected;
}

} // namespace

// ---------------------------------------------------------------------------
// Memory helpers

bool Memory::Read16(uint32_t address, uint16_t& value) {
    uint16_t be = 0;

    if (!Read(address & ~1u, &be, 2))
        return false;

    value = Swap16(be);
    return true;
}

bool Memory::Read32(uint32_t address, uint32_t& value) {
    uint32_t be = 0;

    if (!Read(address & ~3u, &be, 4))
        return false;

    value = Swap32(be);
    return true;
}

bool Memory::Write16(uint32_t address, uint16_t value) {
    const uint16_t be = Swap16(value);
    return Write(address & ~1u, &be, 2);
}

bool Memory::Write32(uint32_t address, uint32_t value) {
    const uint32_t be = Swap32(value);
    return Write(address & ~3u, &be, 4);
}

// ---------------------------------------------------------------------------
// Running a code

std::vector<Line> Plan(const Code& code, Region source) {
    std::vector<Line> plan;
    plan.reserve(code.body.size());

    for (const Line& line : code.body) {
        Line moved = line;

        const Region from = code.sourceRegion != Region::Unknown ? code.sourceRegion : source;

        if (from != Region::Unknown && source != Region::Unknown && from != source) {
            // The handler reads the address as bits 0-24 (bit 0 is its endif
            // flag for ifs), so only bits 24-1 move.
            const Translation translated = TranslateRegion(line.address & 0x01FFFFFEu, from, source);

            if (translated.error == nullptr)
                moved.address = (line.address & 0xFE000001u) | (translated.address & 0x01FFFFFEu);
        }

        plan.push_back(moved);
    }

    return plan;
}

Result Run(const Code& code, Memory& memory, const Options& options) {
    if (!options.ignoreActivator && code.activator && !ActivatorHolds(*code.activator, options.held))
        return Result{};

    if (const std::string reason = UnsupportedReason(code); !reason.empty())
        return Result::Failure(reason);

    Handler handler{memory, code.body};
    const bool ok = handler.Run();

    if (!ok)
        return Result::Failure(handler.error.empty() ? "the code failed" : handler.error);

    Result result;
    result.writes = handler.writes;
    return result;
}

Result RunOnce(const Code& code, Memory& memory) {
    Options options;
    options.ignoreActivator = true;
    options.held = 0xFFFF;
    return Run(code, memory, options);
}

// ---------------------------------------------------------------------------
// The PowerPC interpreter for C0 blocks

namespace {

struct Ppc {
    uint32_t gpr[32];
    uint32_t cr;
    uint32_t lr;
    uint32_t ctr;
    Memory& memory;
    std::string error;
    uint32_t steps;

    bool Fail(std::string message) {
        if (error.empty())
            error = std::move(message);
        return false;
    }

    bool Load(uint32_t address, uint32_t& value, size_t bytes) {
        if (!memory.InRam(address, bytes))
            return Fail("the block reads outside guest RAM");

        switch (bytes) {
        case 1: {
            uint8_t v = 0;

            if (!memory.Read8(address, v))
                return Fail("the block's read failed");

            value = v;
            return true;
        }
        case 2: {
            uint16_t v = 0;

            if (!memory.Read16(address & ~1u, v))
                return Fail("the block's read failed");

            value = v;
            return true;
        }
        default: {
            uint32_t v = 0;

            if (!memory.Read32(address & ~3u, v))
                return Fail("the block's read failed");

            value = v;
            return true;
        }
        }
    }

    bool Store(uint32_t address, uint32_t value, size_t bytes) {
        if (!memory.InRam(address, bytes))
            return Fail("the block writes outside guest RAM");

        if (!memory.Writable(address, bytes))
            return Fail("the block writes the loaded image");

        switch (bytes) {
        case 1:
            return memory.Write8(address, uint8_t(value)) || Fail("the block's write failed");
        case 2:
            return memory.Write16(address & ~1u, uint16_t(value)) || Fail("the block's write failed");
        default:
            return memory.Write32(address & ~3u, value) || Fail("the block's write failed");
        }
    }

    void SetCr0(int32_t value) {
        const uint32_t bits = (value < 0 ? 8u : 0u) | (value > 0 ? 4u : 0u) | (value == 0 ? 2u : 0u);
        cr = (cr & 0x0FFFFFFFu) | (bits << 28);
    }

    void SetCrField(uint32_t field, uint32_t bits) {
        const uint32_t shift = 28 - std::min(field, 7u) * 4;
        cr = (cr & ~(0xFu << shift)) | ((bits & 0xF) << shift);
    }

    bool Run(const std::vector<uint32_t>& words);
};

uint32_t SignExtend(uint32_t value, unsigned bits) {
    const uint32_t sign = 1u << (bits - 1);
    return (value ^ sign) - sign;
}

int32_t AsSigned(uint32_t value) {
    int32_t signedValue = 0;
    std::memcpy(&signedValue, &value, 4);
    return signedValue;
}

uint32_t RotateMask(unsigned mb, unsigned me) {
    const uint32_t left = ~0u >> mb, right = ~0u << (31 - me);
    return mb <= me ? (left & right) : (left | right);
}

// The subset of PowerPC a Gecko C0 block can reasonably carry. An instruction
// it does not know fails the block instead of being skipped.
bool Ppc::Run(const std::vector<uint32_t>& words) {
    size_t pc = 0;

    while (pc < words.size()) {
        if (++steps > kMaxSteps)
            return Fail("the block ran too long");

        const uint32_t word = words[pc++];
        const uint32_t op = word >> 26;
        const uint32_t rd = (word >> 21) & 31, ra = (word >> 16) & 31, rb = (word >> 11) & 31;
        const uint32_t imm = word & 0xFFFF;
        const uint32_t simm = SignExtend(imm, 16);

        switch (op) {
        case 14: // addi
            gpr[rd] = (ra ? gpr[ra] : 0) + simm;
            break;
        case 15: // addis
            gpr[rd] = (ra ? gpr[ra] : 0) + (simm << 16);
            break;
        case 7: // mulli
            gpr[rd] = uint32_t(AsSigned(gpr[ra]) * AsSigned(simm));
            break;
        case 24: // ori
            gpr[ra] = gpr[rd] | imm;
            break;
        case 25: // oris
            gpr[ra] = gpr[rd] | (imm << 16);
            break;
        case 26: // xori
            gpr[ra] = gpr[rd] ^ imm;
            break;
        case 27: // xoris
            gpr[ra] = gpr[rd] ^ (imm << 16);
            break;
        case 28: // andi.
            gpr[ra] = gpr[rd] & imm;
            SetCr0(AsSigned(gpr[ra]));
            break;
        case 29: // andis.
            gpr[ra] = gpr[rd] & (imm << 16);
            SetCr0(AsSigned(gpr[ra]));
            break;
        case 32: { // lwz
            uint32_t value = 0;

            if (!Load(gpr[ra] + simm, value, 4))
                return false;

            gpr[rd] = value;
            break;
        }
        case 34: { // lbz
            uint32_t value = 0;

            if (!Load(gpr[ra] + simm, value, 1))
                return false;

            gpr[rd] = value;
            break;
        }
        case 40: { // lhz
            uint32_t value = 0;

            if (!Load(gpr[ra] + simm, value, 2))
                return false;

            gpr[rd] = value;
            break;
        }
        case 36: // stw
            if (!Store(gpr[ra] + simm, gpr[rd], 4))
                return false;

            break;
        case 38: // stb
            if (!Store(gpr[ra] + simm, gpr[rd], 1))
                return false;

            break;
        case 44: // sth
            if (!Store(gpr[ra] + simm, gpr[rd], 2))
                return false;

            break;
        case 37: // stwu
            return Fail("the block adjusts the stack pointer");
        case 11: { // cmpi
            const int32_t a = AsSigned(gpr[ra]), b = AsSigned(simm);
            SetCrField((word >> 23) & 7, (a < b ? 8u : 0u) | (a > b ? 4u : 0u) | (a == b ? 2u : 0u));
            break;
        }
        case 10: { // cmpli
            const uint32_t a = gpr[ra];
            SetCrField((word >> 23) & 7, (a < imm ? 8u : 0u) | (a > imm ? 4u : 0u) | (a == imm ? 2u : 0u));
            break;
        }
        case 18: { // b / bl
            const uint32_t displacement = SignExtend(word & 0x03FFFFFCu, 26);

            if (word & 1)
                lr = uint32_t(pc * 4);

            pc = size_t(int64_t(pc * 4) + int64_t(displacement)) / 4;
            break;
        }
        case 16: { // bc
            const uint32_t bo = (word >> 21) & 31, bi = (word >> 16) & 31;
            const uint32_t displacement = SignExtend(word & 0xFFFCu, 16);
            bool take;

            if (bo == 20 || bo == 12) {
                take = true;
            } else if (bo == 16 || bo == 8) {
                take = false;
            } else {
                const uint32_t bit = (cr >> (31 - bi)) & 1;
                take = (bo & 1) ? bit != 0 : bit == 0;
            }

            if (take) {
                if (word & 1)
                    lr = uint32_t(pc * 4);

                pc = size_t(int64_t(pc * 4) + int64_t(displacement)) / 4;
            }

            break;
        }
        case 19: {
            const uint32_t xo = (word >> 1) & 0x3FF;

            if (xo == 16) {
                pc = lr / 4;
                break;
            }

            if (xo == 528) {
                pc = ctr / 4;
                break;
            }

            return Fail("the block branches to a register");
        }
        case 21: { // rlwinm
            const uint32_t sh = (word >> 11) & 31, mb = (word >> 6) & 31, me = (word >> 1) & 31;
            const uint32_t rotated = sh == 0 ? gpr[rd] : ((gpr[rd] << sh) | (gpr[rd] >> (32 - sh)));
            gpr[ra] = rotated & RotateMask(mb, me);
            break;
        }
        case 20: { // rlwimi
            const uint32_t sh = (word >> 11) & 31, mb = (word >> 6) & 31, me = (word >> 1) & 31;
            const uint32_t rotated = sh == 0 ? gpr[rd] : ((gpr[rd] << sh) | (gpr[rd] >> (32 - sh)));
            const uint32_t mask = RotateMask(mb, me);
            gpr[ra] = (rotated & mask) | (gpr[ra] & ~mask);
            break;
        }
        case 31: {
            const uint32_t xo = (word >> 1) & 0x3FF;

            switch (xo) {
            case 266: // add
                gpr[rd] = gpr[ra] + gpr[rb];
                break;
            case 40: // subf
                gpr[rd] = gpr[rb] - gpr[ra];
                break;
            case 28: // and
                gpr[ra] = gpr[rd] & gpr[rb];
                break;
            case 444: // or
                gpr[ra] = gpr[rd] | gpr[rb];
                break;
            case 316: // xor
                gpr[ra] = gpr[rd] ^ gpr[rb];
                break;
            case 124: // nor
                gpr[ra] = ~(gpr[rd] | gpr[rb]);
                break;
            case 235: // mullw
                gpr[rd] = uint32_t(int64_t(AsSigned(gpr[ra])) * int64_t(AsSigned(gpr[rb])));
                break;
            case 491: // divw
                if (gpr[rb] == 0 || (AsSigned(gpr[ra]) == INT32_MIN && AsSigned(gpr[rb]) == -1))
                    return Fail("the block divides by zero");

                gpr[rd] = uint32_t(AsSigned(gpr[ra]) / AsSigned(gpr[rb]));
                break;
            case 23: { // lwzx
                uint32_t value = 0;

                if (!Load(gpr[ra] + gpr[rb], value, 4))
                    return false;

                gpr[rd] = value;
                break;
            }
            case 87: { // lbzx
                uint32_t value = 0;

                if (!Load(gpr[ra] + gpr[rb], value, 1))
                    return false;

                gpr[rd] = value;
                break;
            }
            case 279: { // lhzx
                uint32_t value = 0;

                if (!Load(gpr[ra] + gpr[rb], value, 2))
                    return false;

                gpr[rd] = value;
                break;
            }
            case 151: // stwx
                if (!Store(gpr[ra] + gpr[rb], gpr[rd], 4))
                    return false;

                break;
            case 215: // stbx
                if (!Store(gpr[ra] + gpr[rb], gpr[rd], 1))
                    return false;

                break;
            case 407: // sthx
                if (!Store(gpr[ra] + gpr[rb], gpr[rd], 2))
                    return false;

                break;
            case 24: // slw
                gpr[ra] = gpr[rd] << (gpr[rb] & 63);
                break;
            case 536: // srw
                gpr[ra] = gpr[rb] & 63 ? gpr[rd] >> (gpr[rb] & 63) : 0;
                break;
            case 26: // cntlzw
                gpr[ra] = gpr[rd] == 0 ? 32 : uint32_t(__builtin_clz(gpr[rd]));
                break;
            case 0: { // cmp
                const int32_t a = AsSigned(gpr[ra]), b = AsSigned(gpr[rb]);
                SetCrField((word >> 23) & 7, (a < b ? 8u : 0u) | (a > b ? 4u : 0u) | (a == b ? 2u : 0u));
                break;
            }
            case 32: { // cmpl
                const uint32_t a = gpr[ra], b = gpr[rb];
                SetCrField((word >> 23) & 7, (a < b ? 8u : 0u) | (a > b ? 4u : 0u) | (a == b ? 2u : 0u));
                break;
            }
            case 339: // mfspr
            case 467: { // mtspr
                const uint32_t spr = ((word >> 16) & 31) | ((word >> 6) & 0x3E0);

                if (xo == 339) {
                    if (spr == 8)
                        gpr[rd] = lr;
                    else if (spr == 9)
                        gpr[rd] = ctr;
                    else
                        return Fail("the block reads an unsupported special register");
                } else {
                    if (spr == 8)
                        lr = gpr[rd];
                    else if (spr == 9)
                        ctr = gpr[rd];
                    else
                        return Fail("the block writes an unsupported special register");
                }

                break;
            }
            case 19: // mfcr
                gpr[rd] = cr;
                break;
            case 144: { // mtcrf
                const uint32_t mask = (word >> 12) & 0xFF;
                uint32_t next = cr;

                for (unsigned i = 0; i < 8; ++i) {
                    if (!(mask & (1u << (7 - i))))
                        continue;

                    const unsigned shift = (7 - i) * 4;
                    next = (next & ~(0xFu << shift)) | (((gpr[rd] >> shift) & 0xF) << shift);
                }

                cr = next;
                break;
            }
            case 854: // eieio
            case 598: // sync
                break;
            default:
                return Fail("the block uses an instruction the interpreter does not implement");
            }

            break;
        }
        default:
            return Fail("the block uses an opcode the interpreter does not implement");
        }

        if (!error.empty())
            return false;

        if (pc > words.size())
            return Fail("the block branched outside itself");
    }

    return true;
}

} // namespace

BlockResult ExecuteBlock(const std::vector<uint32_t>& words, Memory& memory) {
    BlockResult result;
    Ppc ppc{{}, 0, 0, 0, memory, {}, 0};

    if (!ppc.Run(words)) {
        result.ok = false;
        result.error = ppc.error.empty() ? "the block failed" : ppc.error;
        return result;
    }

    std::memcpy(result.registers, ppc.gpr, sizeof(ppc.gpr));
    return result;
}

bool BlockRunnable(const std::vector<uint32_t>& words, std::string& reason) {
    if (words.empty()) {
        reason = "the code block is empty";
        return false;
    }

    // A block may only read what it wrote itself: the interpreter starts from
    // zeros, and a block written against the game's registers would silently
    // compute wrong values instead of the caller's.
    uint32_t written = 0;

    const auto read = [&](unsigned reg) {
        if (reg == 0)
            return true; // r0 reads as zero in the addressing forms

        if (!(written & (1u << reg))) {
            reason = "the code block reads register r" + std::to_string(reg) + " of the game's own code";
            return false;
        }

        return true;
    };

    const auto write = [&](unsigned reg) { written |= 1u << reg; };

    bool storesOnly = true;

    for (const uint32_t word : words) {
        const uint32_t op = word >> 26;
        const uint32_t rd = (word >> 21) & 31, ra = (word >> 16) & 31, rb = (word >> 11) & 31;

        switch (op) {
        case 14:
        case 15:
            if (ra && !read(ra))
                return false;

            write(rd);
            break;
        case 7:
            if (!read(ra))
                return false;

            write(rd);
            break;
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
            if (!read(rd))
                return false;

            write(ra);
            break;
        case 32:
        case 34:
        case 40:
            if (!read(ra))
                return false;

            write(rd);
            storesOnly = false;
            break;
        case 36:
        case 38:
        case 44:
            if (!read(ra) || !read(rd))
                return false;

            storesOnly = false;
            break;
        case 37:
            reason = "the code block adjusts the stack pointer";
            return false;
        case 11:
        case 10:
            if (!read(ra))
                return false;

            break;
        case 18:
        case 16:
        case 19:
            break;
        case 21:
        case 20:
            if (!read(rd) || !read(ra))
                return false;

            write(ra);
            break;
        case 31: {
            const uint32_t xo = (word >> 1) & 0x3FF;

            switch (xo) {
            case 266:
            case 40:
            case 235:
            case 491:
                if (!read(ra) || !read(rb))
                    return false;

                write(rd);
                break;
            case 28:
            case 444:
            case 316:
            case 124:
            case 24:
            case 536:
                if (!read(rd) || !read(rb))
                    return false;

                write(ra);
                break;
            case 26:
                if (!read(rd))
                    return false;

                write(ra);
                break;
            case 23:
            case 87:
            case 279:
                if (!read(ra) || !read(rb))
                    return false;

                write(rd);
                storesOnly = false;
                break;
            case 151:
            case 215:
            case 407:
                if (!read(ra) || !read(rb) || !read(rd))
                    return false;

                storesOnly = false;
                break;
            case 0:
            case 32:
                if (!read(ra) || !read(rb))
                    return false;

                break;
            case 339:
                write(rd);
                break;
            case 467:
                if (!read(rd))
                    return false;

                break;
            case 19:
                write(rd);
                break;
            case 144:
                if (!read(rd))
                    return false;

                break;
            case 854:
            case 598:
                break;
            default:
                reason = "the code block uses an instruction the interpreter does not implement";
                return false;
            }

            break;
        }
        default:
            reason = "the code block uses an opcode the interpreter does not implement";
            return false;
        }
    }

    // A block that only reads is pointless, and one that never touches memory
    // cannot change the game: refuse both rather than pretend they ran.
    if (storesOnly) {
        reason = "the code block never writes memory";
        return false;
    }

    return true;
}

} // namespace Gecko
