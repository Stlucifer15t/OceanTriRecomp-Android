#pragma once

#include "gecko/gecko_code.h"
#include "gecko/gecko_memory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Gecko {

enum class ExecutionStatus : uint8_t {
    Success,
    UnsupportedInstruction,
    InvalidInstruction,
    MemoryAccessViolation,
    ExecutionLimitReached,
    InvalidFlowControl,
};

struct Limits {
    uint32_t maxInstructions = 4096;
    uint32_t maxConditionDepth = 32;
    uint32_t maxRepeatIterations = 65536;
    uint32_t maxMemoryAccesses = 65536;
    uint32_t maxSearchBytes = 4u * 1024u * 1024u;
    uint32_t maxSearchIterations = 1u * 1024u * 1024u;
};

struct ExecutionError {
    ExecutionStatus status = ExecutionStatus::Success;
    size_t instructionIndex = 0;
    uint32_t address = 0;
    std::string message;
};

struct ExecutionResult {
    ExecutionStatus status = ExecutionStatus::Success;
    size_t instructionsExecuted = 0;
    size_t memoryAccesses = 0;
    size_t repeatIterations = 0;
    size_t searchIterations = 0;
    ExecutionError error;

    bool ok() const { return status == ExecutionStatus::Success; }
};

// The Gecko handler state. `base` and `pointer` are the handler's ba/po;
// `registers` are gr0..gr15. `counters`, `switchState` and `switchLast` are
// per instruction because the reference handler stores them inside the code
// line. `conditions` is the if/endif stack, back() being the current level.
struct State {
    uint32_t base = 0x80000000u;
    uint32_t pointer = 0x80000000u;
    uint32_t registers[16] = {};
    uint32_t repeatCount[16] = {};
    uint32_t repeatAddress[16] = {};
    std::vector<uint32_t> counters;
    std::vector<uint8_t> switchState;
    std::vector<uint8_t> switchLast;
    std::vector<uint8_t> conditions;

    // Resets ba/po/registers/conditions and sizes the per-instruction state
    // for `code`. Counter-if instructions start at the value in their code
    // line, as the reference handler does.
    void Reset(const Code& code);

    // Restores the per-frame handler state: the base address, the pointer
    // and the execution-status stack, which the reference handler resets at
    // the start of every frame. The Gecko registers, repeat slots, counters
    // and on/off switch values live in the handler's code-list memory and
    // persist across frames until the code is disabled or removed. Call this
    // at the start of every logic frame for an enabled code.
    void BeginFrame();

    bool Execute() const { return !conditions.empty() && conditions.back() != 0; }
};

class Interpreter {
  public:
    // Runs one parsed code against `memory`. The caller owns `state`; call
    // State::Reset(code) before a fresh run, or keep it across runs for the
    // reference handler's session behaviour.
    static ExecutionResult Run(const Code& code, Memory& memory, State& state, const Limits& limits = {});
};

const char* ExecutionStatusName(ExecutionStatus status);

} // namespace Gecko
