#pragma once

#include "cheat_manager.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct CpuContext;

// A deliberately restricted Gecko/custom-code importer. This is NOT a PPC
// interpreter or the Gecko code handler: only ordinary RAM writes with an
// explicit, reviewed PAL destination are admitted. No instruction, pointer,
// conditional, repeat, base-address or controller instruction is executed.
namespace CustomCodes {

inline constexpr size_t kMaxCodes = 64;
inline constexpr size_t kMaxInstructions = 256;
inline constexpr size_t kMaxTotalInstructions = 4096;

struct Instruction {
    uint32_t source = 0; // original Wii cached address, never used as a host pointer
    uint32_t target = 0; // explicitly mapped RMHP08 guest RAM address
    uint32_t value = 0;
    uint8_t width = 0;   // 1, 2 or 4 bytes; guest byte order
};

struct Code {
    uint64_t id = 0; // stable identity of name + mapped instructions
    std::string name;
    std::string sourceGame;
    std::filesystem::path file;
    std::vector<Instruction> instructions;
};

struct View {
    std::vector<Code> codes;
    std::vector<uint8_t> enabled;
    std::string status;
};

// Parse a one-code text file and its sidecar .map. Pure and allocation-checked
// by callers; a rejected import cannot partially install a code.
CheatManager::Result Parse(std::string_view text, std::string_view mapping, std::string_view fallbackName, Code& out);
// Main thread before guest execution. Invalid packages are reported, not run.
void Initialize(const std::filesystem::path& userDirectory) noexcept;
void Refresh() noexcept;
View GetView() noexcept;
bool QueueToggle(uint64_t id, bool enabled) noexcept;
void ApplyEnabled(const std::vector<uint64_t>& ids) noexcept;
std::vector<uint64_t> EnabledIds() noexcept;
// Game thread, after CheatManager::Pump and Cheats::Step. Master/offline gates
// are checked here as well as by the caller.
void Pump() noexcept;
void Step(const CpuContext* context) noexcept;
void ResetForTests() noexcept;

} // namespace CustomCodes
