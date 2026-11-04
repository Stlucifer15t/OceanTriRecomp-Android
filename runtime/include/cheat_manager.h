#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Live, offline cheat management. UI/input threads queue commands; only the
// game thread changes native cheat state or guest memory. Configuration is
// separate from the original Wii NAND and never contains guest addresses.
namespace CheatManager {

inline constexpr size_t kMaxNativeCheats = 512;
enum class Category : uint8_t { Player, Items, Combat, Progression, Advanced };
enum class Action : uint8_t { Master, Toggle, Select, DisableAll, RepeatUnlocks, Import, Export, Save, RefreshCustom, ResetLatches };

struct Entry {
    size_t native;
    Category category;
    std::string_view title;
    std::string_view detail;
    bool saveRisk = false;
    bool oneShot = false;
    bool advanced = false;
};

struct Snapshot {
    bool initialized = false;
    bool master = false;
    bool used = false;
    bool online = false; // game-thread mirror of the City/online guard
    size_t count = 0;
    uint64_t revision = 0;
    std::array<uint8_t, kMaxNativeCheats> enabled{};
    std::array<uint32_t, kMaxNativeCheats> selected{};
    std::string status;
};

struct Result {
    bool ok = true;
    std::string message;
};

struct Configuration {
    bool master = false;
    size_t count = 0;
    std::array<uint8_t, kMaxNativeCheats> enabled{};
    std::array<uint32_t, kMaxNativeCheats> selected{};
    std::vector<uint64_t> customEnabled; // stable IDs of explicitly mapped custom codes
};

std::span<const Entry> Catalog() noexcept;
const char* CategoryName(Category category) noexcept;
// Main thread, after CLI parsing and before starting guest execution.
Result Initialize(const std::filesystem::path& userDirectory) noexcept;
// Any thread. Bounded queue; a rejected command has not changed anything.
bool Queue(Action action, size_t native = 0, uint32_t value = 0) noexcept;
bool QueueFile(Action action, const std::filesystem::path& path) noexcept;
// Game thread only, immediately before Cheats::Step.
void Pump() noexcept;
Snapshot GetSnapshot() noexcept;
std::filesystem::path ImportPath() noexcept;
std::filesystem::path ExportPath() noexcept;

// Pure configuration codec, exposed for deterministic host regression tests.
Result DecodeConfig(std::string_view text, Configuration& out) noexcept;
std::string EncodeConfig() noexcept;
Result ExportConfig(const std::filesystem::path& path) noexcept;
Result ImportConfig(const std::filesystem::path& path) noexcept;
void ResetForTests() noexcept;

} // namespace CheatManager
