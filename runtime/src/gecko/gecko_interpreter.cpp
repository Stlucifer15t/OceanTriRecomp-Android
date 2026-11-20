#include "gecko/gecko_interpreter.h"

#include "gecko/gecko_compat.h"

#include <cstring>
#include <string>
#include <utility>

namespace Gecko {
namespace {

constexpr uint32_t kCodeListBase = 0x80001800u;

uint32_t MaskedBase(uint32_t base) {
    // The reference handler keeps only the top seven bits of ba when it forms
    // an address (rlwinm r12, ba, 0, 0, 6).
    return base & 0xFE000000u;
}

bool AddAddress(uint32_t a, uint32_t b, uint32_t& out) {
    const uint64_t sum = uint64_t(a) + uint64_t(b);

    if (sum > 0xFFFFFFFFull)
        return false;

    out = uint32_t(sum);
    return true;
}

int32_t Signed16(uint32_t value) {
    return int32_t(int16_t(value & 0xFFFFu));
}

uint32_t ApplyIntegerOp(uint32_t t, uint32_t lhs, uint32_t rhs) {
    switch (t & 0xFu) {
    case 0:
        return lhs + rhs;
    case 1:
        return lhs * rhs;
    case 2:
        return lhs | rhs;
    case 3:
        return lhs & rhs;
    case 4:
        return lhs ^ rhs;
    case 5:
        return lhs << (rhs & 31u);
    case 6:
        return lhs >> (rhs & 31u);
    case 7: {
        const uint32_t shift = rhs & 31u;
        return shift == 0 ? lhs : (lhs << shift) | (lhs >> (32u - shift));
    }
    case 8:
        return uint32_t(int32_t(lhs) >> (rhs & 31u));
    default:
        return lhs;
    }
}

bool FloatOp(uint32_t t, uint32_t lhsBits, uint32_t rhsBits, uint32_t& outBits) {
    float lhs, rhs;
    std::memcpy(&lhs, &lhsBits, sizeof(lhs));
    std::memcpy(&rhs, &rhsBits, sizeof(rhs));

    float result;

    if ((t & 0xFu) == 9u)
        result = lhs + rhs;
    else if ((t & 0xFu) == 0xAu)
        result = lhs * rhs;
    else
        return false;

    std::memcpy(&outBits, &result, sizeof(outBits));
    return true;
}

class Runner {
  public:
    Runner(const Code& code, Memory& memory, State& state, const Limits& limits)
        : m_code(code), m_memory(memory), m_state(state), m_limits(limits) {}

    ExecutionResult Run() {
        if (m_code.instructions.empty()) {
            Fail(ExecutionStatus::InvalidInstruction, 0, 0, "code has no instructions");
            return m_result;
        }

        if (m_state.conditions.empty())
            m_state.conditions.push_back(1);

        while (m_pc < m_code.instructions.size() && m_result.ok()) {
            if (++m_result.instructionsExecuted > m_limits.maxInstructions) {
                Fail(ExecutionStatus::ExecutionLimitReached, m_pc, 0, "instruction budget exhausted");
                break;
            }

            const Instruction& ins = m_code.instructions[m_pc];
            const Codetype type = Codetype(ins.codetype);
            const CodetypeInfo info = ClassifyCodetype(ins.codetype);

            if (info.support == SupportLevel::Unsupported) {
                Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, std::string("unsupported codetype: ") + info.name);
                break;
            }

            if (info.support == SupportLevel::Unknown) {
                Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "unknown Gecko codetype");
                break;
            }

            if (!Execute(type, ins))
                break;

            if (!m_pcChanged)
                ++m_pc;

            m_pcChanged = false;
        }

        if (m_result.ok() && !m_stopped)
            m_result.status = ExecutionStatus::Success;

        return m_result;
    }

  private:
    bool Fail(ExecutionStatus status, size_t index, uint32_t address, std::string message) {
        m_result.status = status;
        m_result.error.status = status;
        m_result.error.instructionIndex = index;
        m_result.error.address = address;
        m_result.error.message = std::move(message);
        return false;
    }

    bool CheckAccessLimit() {
        if (++m_result.memoryAccesses > m_limits.maxMemoryAccesses) {
            return Fail(ExecutionStatus::ExecutionLimitReached, m_pc, 0, "memory access budget exhausted");
        }

        return true;
    }

    bool Read8(uint32_t address, uint8_t& value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Read8(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 8-bit read");

        return true;
    }

    bool Read16(uint32_t address, uint16_t& value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Read16(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 16-bit read");

        return true;
    }

    bool Read32(uint32_t address, uint32_t& value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Read32(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 32-bit read");

        return true;
    }

    bool Write8(uint32_t address, uint8_t value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Write8(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 8-bit write");

        return true;
    }

    bool Write16(uint32_t address, uint16_t value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Write16(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 16-bit write");

        return true;
    }

    bool Write32(uint32_t address, uint32_t value) {
        if (!CheckAccessLimit())
            return false;

        if (!m_memory.Write32(address, value))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, address, "invalid 32-bit write");

        return true;
    }

    bool PushCondition(bool value) {
        if (m_state.conditions.size() >= m_limits.maxConditionDepth)
            return Fail(ExecutionStatus::ExecutionLimitReached, m_pc, 0, "condition nesting limit reached");

        m_state.conditions.push_back(value ? 1 : 0);
        return true;
    }

    bool PopConditions(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            if (m_state.conditions.size() <= 1)
                return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "endif without a matching if");

            m_state.conditions.pop_back();
        }

        return true;
    }

    bool PopOneIfNeeded(bool endif) {
        return !endif || PopConditions(1);
    }

    void SetCurrentCondition(bool value) {
        if (!m_state.conditions.empty())
            m_state.conditions.back() = value ? 1 : 0;
    }

    uint32_t Base() const { return MaskedBase(m_state.base); }
    uint32_t Pointer() const { return m_state.pointer; }
    uint32_t Offset(const Instruction& ins) const { return ins.address & ~1u; }
    bool EndIf(const Instruction& ins) const { return (ins.address & 1u) != 0; }

    bool ReadRegisterValue(uint32_t reg, uint32_t& value) const {
        if (reg >= 16)
            return false;

        value = m_state.registers[reg];
        return true;
    }

    bool Execute(Codetype type, const Instruction& ins) {
        // If/endif/switch/terminator codes manage the condition stack even
        // when the current level is false; every other code is skipped.
        const bool conditional = IsConditional(type);

        if (!conditional && !m_state.Execute())
            return true;

        switch (type) {
        // Writes.
        case Codetype::Write8:
        case Codetype::Write8Po:
            return Write8Fill(ins, type == Codetype::Write8Po);
        case Codetype::Write16:
        case Codetype::Write16Po:
            return Write16Fill(ins, type == Codetype::Write16Po);
        case Codetype::Write32:
        case Codetype::Write32Po:
            return Write32One(ins, type == Codetype::Write32Po);
        case Codetype::StringWrite:
        case Codetype::StringWritePo:
            return WriteString(ins, type == Codetype::StringWritePo);
        case Codetype::Serial:
        case Codetype::SerialPo:
            return WriteSerial(ins, type == Codetype::SerialPo);

        // Ifs.
        case Codetype::If32Equal:
        case Codetype::If32NotEqual:
        case Codetype::If32Greater:
        case Codetype::If32Lower:
        case Codetype::If32EqualPo:
        case Codetype::If32NotEqualPo:
        case Codetype::If32GreaterPo:
        case Codetype::If32LowerPo:
            return If32(type, ins);
        case Codetype::If16Equal:
        case Codetype::If16NotEqual:
        case Codetype::If16Greater:
        case Codetype::If16Lower:
        case Codetype::If16EqualPo:
        case Codetype::If16NotEqualPo:
        case Codetype::If16GreaterPo:
        case Codetype::If16LowerPo:
            return If16(type, ins);

        // Base/pointer.
        case Codetype::BaseLoad:
        case Codetype::BaseLoadPo:
        case Codetype::BaseSet:
        case Codetype::BaseSetPo:
        case Codetype::BaseStore:
        case Codetype::BaseStorePo:
        case Codetype::PointerLoad:
        case Codetype::PointerLoadPo:
        case Codetype::PointerSet:
        case Codetype::PointerSetPo:
        case Codetype::PointerStore:
        case Codetype::PointerStorePo:
            return BasePointer(type, ins);
        case Codetype::BaseNext:
        case Codetype::PointerNext:
            return NextCodeAddress(type, ins);

        // Flow control.
        case Codetype::RepeatSet:
            return RepeatSet(ins);
        case Codetype::RepeatExecute:
            return RepeatExecute(ins);
        case Codetype::Return:
            return FlowReturn(ins);
        case Codetype::Goto:
            return FlowGoto(ins, false);
        case Codetype::Gosub:
            return FlowGoto(ins, true);

        // Registers.
        case Codetype::RegisterSet:
        case Codetype::RegisterSetPo:
            return RegisterSet(ins, type == Codetype::RegisterSetPo);
        case Codetype::RegisterLoad:
        case Codetype::RegisterLoadPo:
            return RegisterLoad(ins, type == Codetype::RegisterLoadPo);
        case Codetype::RegisterStore:
        case Codetype::RegisterStorePo:
            return RegisterStore(ins, type == Codetype::RegisterStorePo);
        case Codetype::RegisterOp:
        case Codetype::RegisterOpPo:
            return RegisterOp(ins, type == Codetype::RegisterOpPo);
        case Codetype::RegisterOp2:
        case Codetype::RegisterOp2Po:
            return RegisterOp2(ins);
        case Codetype::MemoryCopy1:
        case Codetype::MemoryCopy1Po:
            return MemoryCopy(ins, true, type == Codetype::MemoryCopy1Po);
        case Codetype::MemoryCopy2:
        case Codetype::MemoryCopy2Po:
            return MemoryCopy(ins, false, type == Codetype::MemoryCopy2Po);

        // Register ifs and counter ifs.
        case Codetype::RegisterIfEqual:
        case Codetype::RegisterIfNotEqual:
        case Codetype::RegisterIfGreater:
        case Codetype::RegisterIfLower:
        case Codetype::RegisterIfEqualPo:
        case Codetype::RegisterIfNotEqualPo:
        case Codetype::RegisterIfGreaterPo:
        case Codetype::RegisterIfLowerPo:
            return RegisterIf(type, ins);
        case Codetype::CounterIfEqual:
        case Codetype::CounterIfNotEqual:
        case Codetype::CounterIfGreater:
        case Codetype::CounterIfLower:
            return CounterIf(type, ins);

        // Other.
        case Codetype::OnOffSwitch:
            return OnOffSwitch();
        case Codetype::AddressRange:
        case Codetype::AddressRangePo:
            return AddressRange(type == Codetype::AddressRangePo, ins);
        case Codetype::FullTerminator:
            return FullTerminator(ins);
        case Codetype::EndIf:
            return EndIfCode(ins);
        case Codetype::EndOfCodes:
            m_stopped = true;
            m_pc = m_code.instructions.size();
            m_pcChanged = true;
            return true;
        case Codetype::SearchSetPointer:
            return SearchSetPointer(ins);

        // ASM injection is never executed.
        case Codetype::ExecuteAsm:
        case Codetype::InsertAsm:
        case Codetype::InsertAsmPo:
        case Codetype::CreateBranch:
        case Codetype::CreateBranchPo:
        case Codetype::InsertAsmChecksum:
        case Codetype::InsertAsmChecksumPo:
            return Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, "PPC assembly is not executed by this interpreter");

        default:
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "unhandled codetype");
        }
    }

    static bool IsConditional(Codetype type) {
        switch (type) {
        case Codetype::If32Equal:
        case Codetype::If32NotEqual:
        case Codetype::If32Greater:
        case Codetype::If32Lower:
        case Codetype::If16Equal:
        case Codetype::If16NotEqual:
        case Codetype::If16Greater:
        case Codetype::If16Lower:
        case Codetype::If32EqualPo:
        case Codetype::If32NotEqualPo:
        case Codetype::If32GreaterPo:
        case Codetype::If32LowerPo:
        case Codetype::If16EqualPo:
        case Codetype::If16NotEqualPo:
        case Codetype::If16GreaterPo:
        case Codetype::If16LowerPo:
        case Codetype::RegisterIfEqual:
        case Codetype::RegisterIfNotEqual:
        case Codetype::RegisterIfGreater:
        case Codetype::RegisterIfLower:
        case Codetype::RegisterIfEqualPo:
        case Codetype::RegisterIfNotEqualPo:
        case Codetype::RegisterIfGreaterPo:
        case Codetype::RegisterIfLowerPo:
        case Codetype::CounterIfEqual:
        case Codetype::CounterIfNotEqual:
        case Codetype::CounterIfGreater:
        case Codetype::CounterIfLower:
        case Codetype::OnOffSwitch:
        case Codetype::AddressRange:
        case Codetype::AddressRangePo:
        case Codetype::EndIf:
        case Codetype::FullTerminator:
        case Codetype::EndOfCodes:
        case Codetype::SearchSetPointer:
            return true;
        default:
            return false;
        }
    }

    bool ApplyCondition(bool endif, bool condition) {
        if (!PopOneIfNeeded(endif))
            return false;

        const bool parent = m_state.Execute();
        return PushCondition(parent && condition);
    }

    // --- Writes ------------------------------------------------------------

    bool Write8Fill(const Instruction& ins, bool usePointer) {
        uint32_t address;
        const uint32_t base = usePointer ? Pointer() : Base();

        if (!AddAddress(base, ins.address, address))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

        const uint32_t count = (ins.valueWord >> 16) + 1u;
        const uint8_t value = uint8_t(ins.valueWord & 0xFFu);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t at;

            if (!AddAddress(address, i, at))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            if (!Write8(at, value))
                return false;
        }

        return true;
    }

    bool Write16Fill(const Instruction& ins, bool usePointer) {
        uint32_t address;
        const uint32_t base = usePointer ? Pointer() : Base();

        if (!AddAddress(base, ins.address, address))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

        const uint32_t count = (ins.valueWord >> 16) + 1u;
        const uint16_t value = uint16_t(ins.valueWord & 0xFFFFu);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t at;

            if (!AddAddress(address, i * 2u, at))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            if (!Write16(at, value))
                return false;
        }

        return true;
    }

    bool Write32One(const Instruction& ins, bool usePointer) {
        uint32_t address;
        const uint32_t base = usePointer ? Pointer() : Base();

        if (!AddAddress(base, ins.address, address))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

        return Write32(address, ins.valueWord);
    }

    bool WriteString(const Instruction& ins, bool usePointer) {
        uint32_t address;
        const uint32_t base = usePointer ? Pointer() : Base();

        if (!AddAddress(base, ins.address, address))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

        const uint32_t count = ins.valueWord;

        if (ins.extraWords.size() * 4u < count)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, address, "string write is truncated");

        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t word = ins.extraWords[i / 4u];
            const uint8_t byte = uint8_t(word >> (24u - (i % 4u) * 8u));
            uint32_t at;

            if (!AddAddress(address, i, at))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            if (!Write8(at, byte))
                return false;
        }

        return true;
    }

    bool WriteSerial(const Instruction& ins, bool usePointer) {
        if (ins.extraWords.size() < 2)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "serial write is missing its parameter line");

        uint32_t address;
        const uint32_t base = usePointer ? Pointer() : Base();

        if (!AddAddress(base, ins.address, address))
            return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

        const uint32_t type = (ins.extraWords[0] >> 28) & 0xFu;
        const uint32_t count = (ins.extraWords[0] >> 16) & 0xFFFu;
        const uint32_t stride = ins.extraWords[0] & 0xFFFFu;
        const uint32_t increment = ins.extraWords[1];
        uint32_t value = ins.valueWord;

        for (uint32_t i = 0; i <= count; ++i) {
            uint32_t at;

            if (!AddAddress(address, i * stride, at))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            bool ok;

            if (type == 0)
                ok = Write8(at, uint8_t(value));
            else if (type == 1)
                ok = Write16(at, uint16_t(value));
            else if (type == 2)
                ok = Write32(at, value);
            else
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, at, "invalid serial write size");

            if (!ok)
                return false;

            value += increment;
        }

        return true;
    }

    // --- Ifs ---------------------------------------------------------------

    bool If32(Codetype type, const Instruction& ins) {
        bool usePointer = false;
        uint32_t op;

        switch (type) {
        case Codetype::If32Equal:
            op = 0;
            break;
        case Codetype::If32NotEqual:
            op = 1;
            break;
        case Codetype::If32Greater:
            op = 2;
            break;
        case Codetype::If32Lower:
            op = 3;
            break;
        case Codetype::If32EqualPo:
            op = 0;
            usePointer = true;
            break;
        case Codetype::If32NotEqualPo:
            op = 1;
            usePointer = true;
            break;
        case Codetype::If32GreaterPo:
            op = 2;
            usePointer = true;
            break;
        default:
            op = 3;
            usePointer = true;
            break;
        }

        const bool endif = EndIf(ins);

        if (endif && !PopConditions(1))
            return false;

        bool condition = false;

        if (m_state.Execute()) {
            uint32_t address;
            const uint32_t base = usePointer ? Pointer() : Base();

            if (!AddAddress(base, Offset(ins), address))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            uint32_t value;
            if (!Read32(address, value))
                return false;

            condition = Compare32(op, value, ins.valueWord);
        }

        return PushCondition(m_state.Execute() && condition);
    }

    bool If16(Codetype type, const Instruction& ins) {
        bool usePointer = false;
        uint32_t op;

        switch (type) {
        case Codetype::If16Equal:
            op = 0;
            break;
        case Codetype::If16NotEqual:
            op = 1;
            break;
        case Codetype::If16Greater:
            op = 2;
            break;
        case Codetype::If16Lower:
            op = 3;
            break;
        case Codetype::If16EqualPo:
            op = 0;
            usePointer = true;
            break;
        case Codetype::If16NotEqualPo:
            op = 1;
            usePointer = true;
            break;
        case Codetype::If16GreaterPo:
            op = 2;
            usePointer = true;
            break;
        default:
            op = 3;
            usePointer = true;
            break;
        }

        const bool endif = EndIf(ins);

        if (endif && !PopConditions(1))
            return false;

        bool condition = false;

        if (m_state.Execute()) {
            uint32_t address;
            const uint32_t base = usePointer ? Pointer() : Base();

            if (!AddAddress(base, Offset(ins), address))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            uint16_t value;
            if (!Read16(address, value))
                return false;

            const uint16_t mask = uint16_t(ins.valueWord >> 16);
            const uint16_t expected = uint16_t(ins.valueWord & 0xFFFFu);
            condition = Compare16(op, uint16_t(value & ~mask), expected);
        }

        return PushCondition(m_state.Execute() && condition);
    }

    static bool Compare32(uint32_t op, uint32_t value, uint32_t expected) {
        switch (op) {
        case 0:
            return value == expected;
        case 1:
            return value != expected;
        case 2:
            return value > expected;
        default:
            return value < expected;
        }
    }

    static bool Compare16(uint32_t op, uint16_t value, uint16_t expected) {
        switch (op) {
        case 0:
            return value == expected;
        case 1:
            return value != expected;
        case 2:
            return value > expected;
        default:
            return value < expected;
        }
    }

    // --- Base/pointer ------------------------------------------------------

    bool BasePointer(Codetype type, const Instruction& ins) {
        bool isPointerTarget = false;
        bool isLoad = false;
        bool isStore = false;
        bool poRelative = false;

        switch (type) {
        case Codetype::BaseLoad:
            isLoad = true;
            break;
        case Codetype::BaseLoadPo:
            isLoad = true;
            poRelative = true;
            break;
        case Codetype::BaseSet:
            break;
        case Codetype::BaseSetPo:
            poRelative = true;
            break;
        case Codetype::BaseStore:
            isStore = true;
            break;
        case Codetype::BaseStorePo:
            isStore = true;
            poRelative = true;
            break;
        case Codetype::PointerLoad:
            isLoad = true;
            isPointerTarget = true;
            break;
        case Codetype::PointerLoadPo:
            isLoad = true;
            isPointerTarget = true;
            poRelative = true;
            break;
        case Codetype::PointerSet:
            isPointerTarget = true;
            break;
        case Codetype::PointerSetPo:
            isPointerTarget = true;
            poRelative = true;
            break;
        case Codetype::PointerStore:
            isStore = true;
            isPointerTarget = true;
            break;
        default:
            isStore = true;
            isPointerTarget = true;
            poRelative = true;
            break;
        }

        const uint32_t add = (ins.addressWord >> 20) & 0xFu;
        const uint32_t useBase = (ins.addressWord >> 16) & 0xFu;
        const uint32_t useRegister = (ins.addressWord >> 12) & 0xFu;
        const uint32_t regIndex = ins.addressWord & 0xFu;

        const uint32_t base = poRelative ? Pointer() : Base();
        uint32_t operand = ins.valueWord;

        if (useRegister != 0) {
            uint32_t reg;
            if (!ReadRegisterValue(regIndex, reg))
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");
            if (!AddAddress(operand, reg, operand))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
        }

        if (poRelative || useBase != 0) {
            if (!AddAddress(operand, base, operand))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
        }

        uint32_t& target = isPointerTarget ? m_state.pointer : m_state.base;

        if (isStore) {
            return Write32(operand, target);
        }

        if (isLoad) {
            uint32_t value;
            if (!Read32(operand, value))
                return false;

            if (add != 0) {
                uint32_t sum;
                if (!AddAddress(target, value, sum))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
                target = sum;
            } else {
                target = value;
            }

            return true;
        }

        if (add != 0) {
            uint32_t sum;
            if (!AddAddress(target, operand, sum))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            target = sum;
        } else {
            target = operand;
        }

        return true;
    }

    bool NextCodeAddress(Codetype type, const Instruction& ins) {
        const int32_t delta = Signed16(ins.addressWord & 0xFFFFu);
        const int64_t next = int64_t(kCodeListBase) + int64_t(m_pc + 1) * 8 + int64_t(delta) * 8;

        if (next < 0 || next > 0xFFFFFFFFll)
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "code address overflow");

        if (type == Codetype::BaseNext)
            m_state.base = uint32_t(next);
        else
            m_state.pointer = uint32_t(next);

        return true;
    }

    // --- Flow control ------------------------------------------------------

    bool RepeatSet(const Instruction& ins) {
        const uint32_t slot = ins.valueWord & 0xFu;
        if (slot >= 16)
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "invalid repeat slot");

        m_state.repeatAddress[slot] = uint32_t(m_pc + 1);
        m_state.repeatCount[slot] = ins.addressWord & 0xFFFFu;
        return true;
    }

    bool RepeatExecute(const Instruction& ins) {
        const uint32_t slot = ins.valueWord & 0xFu;
        if (slot >= 16)
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "invalid repeat slot");

        if (++m_result.repeatIterations > m_limits.maxRepeatIterations)
            return Fail(ExecutionStatus::ExecutionLimitReached, m_pc, 0, "repeat iteration limit reached");

        if (m_state.repeatCount[slot] == 0)
            return true;

        --m_state.repeatCount[slot];
        m_pc = m_state.repeatAddress[slot];
        m_pcChanged = true;
        return true;
    }

    bool ShouldFlow(uint32_t s) {
        const bool execute = m_state.Execute();

        if (s == 0)
            return execute;
        if (s == 1)
            return !execute;

        return true;
    }

    bool FlowReturn(const Instruction& ins) {
        const uint32_t slot = ins.valueWord & 0xFu;
        if (slot >= 16)
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "invalid return slot");

        const uint32_t s = (ins.addressWord >> 20) & 0xFu;

        if (!ShouldFlow(s))
            return true;

        if (m_state.repeatAddress[slot] == 0)
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "return without a gosub");

        m_pc = m_state.repeatAddress[slot];
        m_pcChanged = true;
        return true;
    }

    bool FlowGoto(const Instruction& ins, bool gosub) {
        const uint32_t s = (ins.addressWord >> 20) & 0xFu;
        const int32_t delta = Signed16(ins.addressWord & 0xFFFFu);

        if (!ShouldFlow(s))
            return true;

        if (gosub) {
            const uint32_t slot = ins.valueWord & 0xFu;
            if (slot >= 16)
                return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "invalid gosub slot");
            m_state.repeatAddress[slot] = uint32_t(m_pc + 1);
        }

        const int64_t target = int64_t(m_pc) + 1 + delta;

        if (target < 0 || target >= int64_t(m_code.instructions.size()))
            return Fail(ExecutionStatus::InvalidFlowControl, m_pc, 0, "flow-control jump out of range");

        m_pc = size_t(target);
        m_pcChanged = true;
        return true;
    }

    // --- Registers ---------------------------------------------------------

    bool RegisterSet(const Instruction& ins, bool poRelative) {
        const uint32_t add = (ins.addressWord >> 20) & 0xFu;
        const uint32_t useBase = (ins.addressWord >> 16) & 0xFu;
        const uint32_t regIndex = ins.addressWord & 0xFu;

        if (regIndex >= 16)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");

        uint32_t value = ins.valueWord;

        if (useBase != 0) {
            uint32_t base = poRelative ? Pointer() : Base();
            if (!AddAddress(value, base, value))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
        }

        if (add != 0) {
            uint32_t sum;
            if (!AddAddress(m_state.registers[regIndex], value, sum))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            m_state.registers[regIndex] = sum;
        } else {
            m_state.registers[regIndex] = value;
        }

        return true;
    }

    bool RegisterLoad(const Instruction& ins, bool poRelative) {
        const uint32_t size = (ins.addressWord >> 20) & 0xFu;
        const uint32_t useBase = (ins.addressWord >> 16) & 0xFu;
        const uint32_t regIndex = ins.addressWord & 0xFu;

        if (regIndex >= 16)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");

        uint32_t address = ins.valueWord;

        if (useBase != 0) {
            uint32_t base = poRelative ? Pointer() : Base();
            if (!AddAddress(address, base, address))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
        }

        if (size == 0) {
            uint8_t value;
            if (!Read8(address, value))
                return false;
            m_state.registers[regIndex] = value;
        } else if (size == 1) {
            uint16_t value;
            if (!Read16(address, value))
                return false;
            m_state.registers[regIndex] = value;
        } else if (size == 2) {
            uint32_t value;
            if (!Read32(address, value))
                return false;
            m_state.registers[regIndex] = value;
        } else {
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, address, "invalid register load size");
        }

        return true;
    }

    bool RegisterStore(const Instruction& ins, bool poRelative) {
        const uint32_t size = (ins.addressWord >> 20) & 0xFu;
        const uint32_t useBase = (ins.addressWord >> 16) & 0xFu;
        const uint32_t count = (ins.addressWord >> 4) & 0xFFFu;
        const uint32_t regIndex = ins.addressWord & 0xFu;

        if (regIndex >= 16)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");

        uint32_t address = ins.valueWord;

        if (useBase != 0) {
            uint32_t base = poRelative ? Pointer() : Base();
            if (!AddAddress(address, base, address))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
        }

        uint32_t value = m_state.registers[regIndex];
        const uint32_t element = size == 0 ? 1u : size == 1 ? 2u : 4u;

        if (size > 2)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, address, "invalid register store size");

        for (uint32_t i = 0; i <= count; ++i) {
            uint32_t at;

            if (!AddAddress(address, i * element, at))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            bool ok;

            if (size == 0)
                ok = Write8(at, uint8_t(value));
            else if (size == 1)
                ok = Write16(at, uint16_t(value));
            else
                ok = Write32(at, value);

            if (!ok)
                return false;
        }

        return true;
    }

    bool ApplyRegisterOp(uint32_t op, uint32_t lhs, uint32_t rhs, uint32_t& out) {
        if ((op & 0xFu) >= 9u)
            return FloatOp(op, lhs, rhs, out);

        out = ApplyIntegerOp(op, lhs, rhs);
        return true;
    }

    bool RegisterOp(const Instruction& ins, bool) {
        const uint32_t op = (ins.addressWord >> 20) & 0xFu;
        const uint32_t mode = (ins.addressWord >> 16) & 0xFu;
        const uint32_t regIndex = ins.addressWord & 0xFu;

        if (regIndex >= 16)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");

        if ((op & 0xFu) >= 0xBu)
            return Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, "unsupported register operation");

        uint32_t lhs = m_state.registers[regIndex];
        uint32_t rhs = ins.valueWord;
        uint32_t destAddress = 0;
        bool destMemory = false;

        if (mode == 1 || mode == 3) {
            destAddress = m_state.registers[regIndex];
            destMemory = true;
            if (!Read32(destAddress, lhs))
                return false;
        }

        if (mode == 2 || mode == 3) {
            if (!Read32(rhs, rhs))
                return false;
        }

        uint32_t result;
        if (!ApplyRegisterOp(op, lhs, rhs, result))
            return Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, "unsupported register operation");

        if (destMemory)
            return Write32(destAddress, result);

        m_state.registers[regIndex] = result;
        return true;
    }

    bool RegisterOp2(const Instruction& ins) {
        const uint32_t op = (ins.addressWord >> 20) & 0xFu;
        const uint32_t mode = (ins.addressWord >> 16) & 0xFu;
        const uint32_t regN = ins.addressWord & 0xFu;
        const uint32_t regK = ins.valueWord & 0xFu;

        if (regN >= 16 || regK >= 16)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");

        if ((op & 0xFu) >= 0xBu)
            return Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, "unsupported register operation");

        uint32_t lhs = m_state.registers[regN];
        uint32_t rhs = m_state.registers[regK];
        uint32_t destAddress = 0;
        bool destMemory = false;

        if (mode == 1 || mode == 3) {
            destAddress = m_state.registers[regN];
            destMemory = true;
            if (!Read32(destAddress, lhs))
                return false;
        }

        if (mode == 2 || mode == 3) {
            if (!Read32(m_state.registers[regK], rhs))
                return false;
        }

        uint32_t result;
        if (!ApplyRegisterOp(op, lhs, rhs, result))
            return Fail(ExecutionStatus::UnsupportedInstruction, m_pc, ins.address, "unsupported register operation");

        if (destMemory)
            return Write32(destAddress, result);

        m_state.registers[regN] = result;
        return true;
    }

    bool MemoryCopy(const Instruction& ins, bool copy1, bool poRelative) {
        const uint32_t count = (ins.addressWord >> 16) & 0xFFFFu;
        const uint32_t n = (ins.addressWord >> 12) & 0xFu;
        const uint32_t k = (ins.addressWord >> 8) & 0xFu;

        uint32_t base = poRelative ? Pointer() : Base();
        uint32_t source;
        uint32_t destination;

        if (copy1) {
            if (n >= 16)
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid source register");
            source = m_state.registers[n];

            if (k == 0xFu) {
                if (!AddAddress(base, ins.valueWord, destination))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else if (k < 16) {
                if (!AddAddress(m_state.registers[k], ins.valueWord, destination))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else {
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid destination register");
            }
        } else {
            if (k >= 16)
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid destination register");
            destination = m_state.registers[k];

            if (n == 0xFu) {
                if (!AddAddress(base, ins.valueWord, source))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else if (n < 16) {
                if (!AddAddress(m_state.registers[n], ins.valueWord, source))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else {
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid source register");
            }
        }

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t from, to;
            uint8_t value;

            if (!AddAddress(source, i, from) || !AddAddress(destination, i, to))
                return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");

            if (!Read8(from, value))
                return false;

            if (!Write8(to, value))
                return false;
        }

        return true;
    }

    // --- Register/counter ifs ---------------------------------------------

    bool RegisterIf(Codetype type, const Instruction& ins) {
        bool poRelative = false;
        uint32_t op;

        switch (type) {
        case Codetype::RegisterIfEqual:
            op = 0;
            break;
        case Codetype::RegisterIfNotEqual:
            op = 1;
            break;
        case Codetype::RegisterIfGreater:
            op = 2;
            break;
        case Codetype::RegisterIfLower:
            op = 3;
            break;
        case Codetype::RegisterIfEqualPo:
            op = 0;
            poRelative = true;
            break;
        case Codetype::RegisterIfNotEqualPo:
            op = 1;
            poRelative = true;
            break;
        case Codetype::RegisterIfGreaterPo:
            op = 2;
            poRelative = true;
            break;
        default:
            op = 3;
            poRelative = true;
            break;
        }

        const bool endif = EndIf(ins);

        if (endif && !PopConditions(1))
            return false;

        bool condition = false;

        if (m_state.Execute()) {
            const uint32_t n = (ins.valueWord >> 24) & 0xFu;
            const uint32_t k = (ins.valueWord >> 28) & 0xFu;
            const uint16_t mask = uint16_t(ins.valueWord & 0xFFFFu);
            uint32_t addressN, addressK;
            uint32_t base = poRelative ? Pointer() : Base();

            if (n == 0xFu) {
                if (!AddAddress(base, Offset(ins), addressN))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else if (n < 16) {
                addressN = m_state.registers[n];
            } else {
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");
            }

            if (k == 0xFu) {
                if (!AddAddress(base, Offset(ins), addressK))
                    return Fail(ExecutionStatus::MemoryAccessViolation, m_pc, 0, "address overflow");
            } else if (k < 16) {
                addressK = m_state.registers[k];
            } else {
                return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "invalid gecko register");
            }

            uint16_t valueN, valueK;
            if (!Read16(addressN, valueN) || !Read16(addressK, valueK))
                return false;

            condition = Compare16(op, uint16_t(valueN & ~mask), uint16_t(valueK & ~mask));
        }

        return PushCondition(m_state.Execute() && condition);
    }

    bool CounterIf(Codetype type, const Instruction& ins) {
        const uint32_t t = ins.addressWord & 0xFu;
        const uint16_t mask = uint16_t(ins.valueWord >> 16);
        const uint16_t value = uint16_t(ins.valueWord & 0xFFFFu);

        if ((t & 1u) != 0 && !PopConditions(1))
            return false;

        bool condition = false;

        if (m_state.Execute()) {
            ++m_state.counters[m_pc];

            const uint16_t counter = uint16_t(m_state.counters[m_pc]);
            const uint16_t masked = uint16_t(counter & ~mask);
            uint32_t op;

            switch (type) {
            case Codetype::CounterIfEqual:
                op = 0;
                break;
            case Codetype::CounterIfNotEqual:
                op = 1;
                break;
            case Codetype::CounterIfGreater:
                op = 2;
                break;
            default:
                op = 3;
                break;
            }

            condition = Compare16(op, masked, value);

            if ((t & 8u) != 0 && condition)
                m_state.counters[m_pc] = 0;
        } else {
            m_state.counters[m_pc] = 0;
        }

        return PushCondition(m_state.Execute() && condition);
    }

    // --- Other -------------------------------------------------------------

    bool OnOffSwitch() {
        const bool current = m_state.Execute();
        const bool last = m_state.switchLast[m_pc] != 0;

        if (!last && current)
            m_state.switchState[m_pc] = uint8_t(!m_state.switchState[m_pc]);

        m_state.switchLast[m_pc] = current ? 1 : 0;
        SetCurrentCondition(m_state.switchState[m_pc] != 0);
        return true;
    }

    bool AddressRange(bool poRelative, const Instruction& ins) {
        const uint32_t t = ins.addressWord & 0xFu;

        if ((t & 1u) != 0 && !PopConditions(1))
            return false;

        bool condition = false;

        if (m_state.Execute()) {
            const uint32_t start = (ins.valueWord >> 16) << 16;
            const uint32_t end = (ins.valueWord & 0xFFFFu) << 16;
            const uint32_t value = poRelative ? Pointer() : Base();
            condition = start < end && value >= start && value < end;
        }

        return PushCondition(m_state.Execute() && condition);
    }

    bool FullTerminator(const Instruction& ins) {
        m_state.conditions.assign(1, 0);

        const uint32_t high = ins.valueWord >> 16;
        const uint32_t low = ins.valueWord & 0xFFFFu;

        if (high != 0)
            m_state.base = high << 16;
        if (low != 0)
            m_state.pointer = low << 16;

        return true;
    }

    bool EndIfCode(const Instruction& ins) {
        const uint32_t count = ins.addressWord & 0xFFu;
        const uint32_t t = (ins.addressWord >> 20) & 0xFu;

        if (!PopConditions(count))
            return false;

        if (t == 1)
            SetCurrentCondition(!m_state.Execute());

        const uint32_t high = ins.valueWord >> 16;
        const uint32_t low = ins.valueWord & 0xFFFFu;

        if (high != 0)
            m_state.base = high << 16;
        if (low != 0)
            m_state.pointer = low << 16;

        return true;
    }

    bool SearchSetPointer(const Instruction& ins) {
        const uint32_t lines = ins.addressWord & 0xFFu;
        const uint32_t patternWords = lines * 2u;

        if (ins.extraWords.size() < patternWords)
            return Fail(ExecutionStatus::InvalidInstruction, m_pc, ins.address, "search pattern is truncated");

        uint32_t start = (ins.valueWord >> 16) << 16;
        const uint32_t end = (ins.valueWord & 0xFFFFu) << 16;

        if ((ins.valueWord >> 16) == 0x8000u)
            start = 0x80003000u;

        if (lines == 0) {
            return PushCondition(m_state.Execute());
        }

        if (start >= end) {
            return PushCondition(false);
        }

        if (uint64_t(end) - start > m_limits.maxSearchBytes)
            return Fail(ExecutionStatus::ExecutionLimitReached, m_pc, start, "search range exceeds the limit");

        if (!m_state.Execute())
            return PushCondition(false);

        bool found = false;
        uint32_t foundAddress = 0;
        const uint64_t patternBytes = uint64_t(patternWords) * 4u;

        for (uint64_t address = start; address + patternBytes <= end; address += 4u) {
            if (++m_result.searchIterations > m_limits.maxSearchIterations)
                return Fail(ExecutionStatus::ExecutionLimitReached, m_pc, uint32_t(address), "search iteration limit reached");

            bool match = true;

            for (uint32_t i = 0; i < patternWords; ++i) {
                uint32_t word;

                if (!Read32(uint32_t(address + uint64_t(i) * 4u), word)) {
                    match = false;
                    break;
                }

                if (word != ins.extraWords[i]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                found = true;
                foundAddress = uint32_t(address);
                break;
            }
        }

        if (found)
            m_state.pointer = foundAddress;

        return PushCondition(found);
    }

    const Code& m_code;
    Memory& m_memory;
    State& m_state;
    Limits m_limits;
    ExecutionResult m_result;
    size_t m_pc = 0;
    bool m_stopped = false;
    bool m_pcChanged = false;
};

} // namespace

void State::Reset(const Code& code) {
    base = 0x80000000u;
    pointer = 0x80000000u;

    for (uint32_t& reg : registers)
        reg = 0;
    for (uint32_t& count : repeatCount)
        count = 0;
    for (uint32_t& address : repeatAddress)
        address = 0;

    counters.assign(code.instructions.size(), 0);
    switchState.assign(code.instructions.size(), 0);
    switchLast.assign(code.instructions.size(), 0);

    for (size_t i = 0; i < code.instructions.size(); ++i) {
        const Instruction& ins = code.instructions[i];

        if (ins.codetype == uint8_t(Codetype::CounterIfEqual) || ins.codetype == uint8_t(Codetype::CounterIfNotEqual) ||
            ins.codetype == uint8_t(Codetype::CounterIfGreater) || ins.codetype == uint8_t(Codetype::CounterIfLower)) {
            counters[i] = (ins.addressWord >> 4) & 0xFFFFu;
        }
    }

    conditions.assign(1, 1);
}

void State::BeginFrame() {
    // The reference handler resets ba, po and the execution-status stack at
    // the start of every frame. The Gecko registers, repeat slots, counters
    // and on/off switch values live in the handler's code-list memory and
    // persist across frames until the code is disabled or removed.
    base = 0x80000000u;
    pointer = 0x80000000u;
    conditions.assign(1, 1);
}

ExecutionResult Interpreter::Run(const Code& code, Memory& memory, State& state, const Limits& limits) {
    if (state.counters.size() != code.instructions.size())
        state.Reset(code);

    if (state.switchState.size() != code.instructions.size())
        state.switchState.assign(code.instructions.size(), 0);

    if (state.switchLast.size() != code.instructions.size())
        state.switchLast.assign(code.instructions.size(), 0);

    Runner runner(code, memory, state, limits);
    return runner.Run();
}

const char* ExecutionStatusName(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::Success:
        return "success";
    case ExecutionStatus::UnsupportedInstruction:
        return "unsupported instruction";
    case ExecutionStatus::InvalidInstruction:
        return "invalid instruction";
    case ExecutionStatus::MemoryAccessViolation:
        return "memory access violation";
    case ExecutionStatus::ExecutionLimitReached:
        return "execution limit reached";
    case ExecutionStatus::InvalidFlowControl:
        return "invalid flow control";
    }

    return "unknown";
}

} // namespace Gecko
