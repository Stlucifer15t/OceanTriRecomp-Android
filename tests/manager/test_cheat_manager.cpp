#include "cheat_manager.h"
#include "custom_codes.h"
#include "cheats.h"
#include "launcher_cheats.h"
#include "player_health.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
unsigned g_stepPasses = 0;
bool g_noDamage = false;
bool g_noStamina = false;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), std::streamsize(text.size()));
    Check(bool(out), "fixture write");
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

size_t Index(Cheats::Id id) { return size_t(id); }
} // namespace

// Isolate the switch/queue/codec layer from guest execution. Native effect
// tests are separate; these stubs only record that Step reached the game pass.
namespace Cheats {
void StepSaveData() { ++g_stepPasses; }
void StepEquipment() {}
void StepItems() {}
void StepHunterCodes() {}
void StepPlayer() {}
void StepMonsters() {}
void StepMovementCodes() {}
void StepRequestedUnlocks(CpuContext*) {}
}
namespace PlayerHealth { void SetNoDamage(bool enabled) { g_noDamage = enabled; } }
namespace LauncherCheats { void SetNoStaminaDrain(bool enabled) { g_noStamina = enabled; } }

int main() {
    try {
        Memory::Init(Memory::kMem1Size);
        const auto root = std::filesystem::temp_directory_path() /
            ("oceantri-cheats-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);

        const auto catalog = CheatManager::Catalog();
        bool categories[5]{};
        for (const auto& entry : catalog) {
            Check(entry.native < LauncherCheats::List().size(), "catalog native index");
            categories[size_t(entry.category)] = true;
        }
        for (bool present : categories)
            Check(present, "all requested categories plus advanced are present");

        CheatManager::ResetForTests();
        Check(CheatManager::Initialize(root).ok, "initialize empty profile");
        Check(!CheatManager::GetSnapshot().master, "safe default master OFF");
        Check(!Cheats::g_used.load(), "unused profile does not block online admission");

        Check(CheatManager::Queue(CheatManager::Action::Toggle, Index(Cheats::Id::InfiniteHealth), 1), "queue toggle");
        Check(!CheatManager::GetSnapshot().enabled[Index(Cheats::Id::InfiniteHealth)], "UI queue does not mutate game state");
        CheatManager::Pump();
        Check(CheatManager::GetSnapshot().enabled[Index(Cheats::Id::InfiniteHealth)], "game-thread pump applies toggle");
        Check(!Cheats::On(Cheats::Id::InfiniteHealth), "master OFF gates native hook");
        Check(!Cheats::g_used.load(), "selecting while master OFF is not use");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 1), "queue master");
        CheatManager::Pump();
        Check(Cheats::On(Cheats::Id::InfiniteHealth), "master ON activates selected hook");
        Check(Cheats::g_used.load(), "actual use is sticky for online guard");

        const unsigned before = g_stepPasses;
        Cheats::Step(nullptr);
        Check(g_stepPasses == before + 1, "first live per-step cheat runs");
        Check(CheatManager::Queue(CheatManager::Action::Toggle, Index(Cheats::Id::InfiniteStamina), 1), "enable after first Step");
        CheatManager::Pump();
        Cheats::Step(nullptr);
        Check(g_stepPasses == before + 2, "no cached startup any-cheat decision");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 0), "master off");
        CheatManager::Pump();
        Cheats::Step(nullptr);
        Check(g_stepPasses == before + 2, "master OFF stops per-step writes");
        Check(!Cheats::On(Cheats::Id::InfiniteStamina), "master OFF gates all hooks");

        Check(LauncherCheats::EnableByName("No damage", ""), "legacy CLI name");
        Check(LauncherCheats::EnableByName("No action stamina drain", ""), "legacy stamina name");
        Check(!g_noDamage && !g_noStamina, "port flags remain gated while master OFF");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 1), "activate legacy flags");
        CheatManager::Pump();
        Check(g_noDamage && g_noStamina, "existing port flags synchronized");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 0), "master off again");
        CheatManager::Pump();
        Check(!g_noDamage && !g_noStamina, "master OFF clears ordinary port flags");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 1), "master on again");
        CheatManager::Pump();
        Check(g_noDamage && g_noStamina, "master ON restores ordinary port flags");

        Check(CheatManager::Queue(CheatManager::Action::Toggle, Index(Cheats::Id::MaxMoney), 1), "money toggle");
        const size_t ranged = Index(Cheats::Id::MonsterHpMultiplier);
        Check(LauncherCheats::Choices(ranged) > 2, "range fixture has choices");
        Check(CheatManager::Queue(CheatManager::Action::Select, ranged, 2), "queue choice");
        CheatManager::Pump();
        const auto exported = root / "export.oceantri";
        Check(CheatManager::ExportConfig(exported).ok, "export profile");
        const auto text = Read(exported);
        Check(text.find("game RMHP08") != std::string::npos, "region recorded");
        Check(text.find("native " + std::to_string(Index(Cheats::Id::MaxMoney)) + " 1") != std::string::npos, "enabled state exported");

        CheatManager::Configuration decoded;
        decoded.count = 12345;
        Check(!CheatManager::DecodeConfig(text + "native 999999 1 0\n", decoded).ok, "out-of-range rejected");
        Check(decoded.count == 12345, "failed decode leaves output untouched");
        Check(!CheatManager::DecodeConfig(text + "native 0 1 0\n", decoded).ok, "duplicate rejected");
        Check(!CheatManager::DecodeConfig("OCEANTRI-CHEATS 1\ngame RMHE08\nmaster 0\n", decoded).ok, "wrong region rejected");
        Check(!CheatManager::DecodeConfig(text + "asm 80000000 60000000\n", decoded).ok, "unknown fields rejected");
        Check(!CheatManager::DecodeConfig(std::string(65537, 'x'), decoded).ok, "oversize rejected");
        Check(CheatManager::DecodeConfig(text, decoded).ok, "valid round trip");
        Check(decoded.enabled[Index(Cheats::Id::MaxMoney)] == 1, "decoded enable");
        Check(decoded.selected[ranged] == 2, "decoded choice");

        CheatManager::ResetForTests();
        const auto importRoot = root / "fresh-import";
        Check(CheatManager::Initialize(importRoot).ok, "reinitialize before import");
        Check(CheatManager::ImportConfig(exported).ok, "import profile");
        Check(!CheatManager::GetSnapshot().master, "import never self-activates");
        Check(Cheats::g_on[Index(Cheats::Id::MaxMoney)], "import restores selection");
        Check(!Cheats::On(Cheats::Id::MaxMoney), "imported selection gated until review");
        Check(!Cheats::g_used.load(), "inactive import does not block online");
        Check(CheatManager::Queue(CheatManager::Action::DisableAll), "disable all");
        CheatManager::Pump();
        Check(LauncherCheats::EnabledCount() == 0, "disable all clears every switch");
        Check(!Cheats::g_used.load(), "an imported profile that was never activated is still unused");
        Check(CheatManager::Queue(CheatManager::Action::Toggle, Index(Cheats::Id::MaxMoney), 1), "select money");
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 1), "activate money");
        CheatManager::Pump();
        Check(CheatManager::Queue(CheatManager::Action::DisableAll), "disable after actual use");
        CheatManager::Pump();
        Check(Cheats::g_used.load(), "disable all does not launder modified state for online");

        // Existing --cheat/--no-damage behavior survives profile initialization.
        CheatManager::ResetForTests();
        Check(LauncherCheats::EnableByName("No damage", ""), "CLI before initialize");
        Write(root / "cheats" / "active.oceantri", "OCEANTRI-CHEATS 1\ngame RMHP08\nmaster 0\n");
        Check(CheatManager::Initialize(root).ok, "initialize with CLI and inactive profile");
        Check(CheatManager::GetSnapshot().master, "CLI explicitly activates cheats");
        Check(Cheats::On(Cheats::Id::NoDamage), "CLI selection preserved");

        // Combined native + explicitly mapped custom profiles round-trip.
        CheatManager::ResetForTests();
        const auto customRoot = root / "custom-profile";
        Check(CheatManager::Initialize(customRoot).ok, "initialize custom profile root");
        Write(customRoot / "cheats" / "gecko" / "sample.txt", "$Sample mapped write\n0265AA62 00001234\n");
        Write(customRoot / "cheats" / "gecko" / "sample.map",
              "OCEANTRI-GECKO-MAP 1\nsource RMHP08\ntarget RMHP08\nmap 8065AA62 8065CE62 2\n");
        Check(CheatManager::Queue(CheatManager::Action::RefreshCustom), "queue custom rescan");
        CheatManager::Pump();
        const auto customView = CustomCodes::GetView();
        Check(customView.codes.size() == 1, "custom package imported through manager");
        Check(CustomCodes::QueueToggle(customView.codes[0].id, true), "custom toggle queued");
        CheatManager::Pump();
        const auto combined = customRoot / "combined.oceantri";
        Check(CheatManager::ExportConfig(combined).ok, "export combined profile");
        const auto combinedText = Read(combined);
        Check(combinedText.find("custom ") != std::string::npos, "custom identity exported");
        CheatManager::Configuration combinedConfig;
        Check(CheatManager::DecodeConfig(combinedText, combinedConfig).ok && combinedConfig.customEnabled.size() == 1, "combined profile decodes");
        CheatManager::ResetForTests();
        Check(CheatManager::Initialize(customRoot).ok, "reinitialize before combined import");
        Check(CheatManager::ImportConfig(combined).ok, "combined import");
        Check(!CheatManager::GetSnapshot().master && CustomCodes::EnabledIds().size() == 1, "combined import stays master OFF but restores custom selection");
        auto unknown = combinedText;
        const auto at = unknown.find("custom ");
        unknown.replace(at + 7, unknown.find(' ', at + 7) - (at + 7), "1");
        Write(customRoot / "unknown.oceantri", unknown);
        Check(!CheatManager::ImportConfig(customRoot / "unknown.oceantri").ok, "unknown custom identity rejected");
        Check(CustomCodes::EnabledIds().size() == 1, "failed import leaves custom selection untouched");
        Check(CheatManager::Queue(CheatManager::Action::ResetLatches), "queue latch reset");
        CheatManager::Pump();
        Memory::Write8(0x8065AA84u, 2);
        Check(CheatManager::Queue(CheatManager::Action::Master, 0, 1), "queue master during online session");
        CheatManager::Pump();
        Check(!CheatManager::GetSnapshot().master, "online session refuses master enable");
        Memory::Write8(0x8065AA84u, 0);

        // Bounded queue and concurrent producers cannot write native arrays.
        CheatManager::ResetForTests();
        Check(CheatManager::Initialize(root).ok, "initialize queue test");
        std::vector<std::thread> producers;
        for (unsigned t = 0; t < 4; ++t)
            producers.emplace_back([] { for (unsigned i = 0; i < 16; ++i) Check(CheatManager::Queue(CheatManager::Action::Save), "bounded concurrent queue"); });
        for (auto& thread : producers)
            thread.join();
        Check(!CheatManager::Queue(CheatManager::Action::Save), "queue rejects overflow");
        CheatManager::Pump();
        Check(CheatManager::Queue(CheatManager::Action::Save), "pump drains queue");
        CheatManager::Pump();

        std::filesystem::remove_all(root);
        std::cout << "Cheat manager regression tests passed (catalog, live gates, Step, profiles, CLI, queue).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
