#include "custom_codes.h"
#include "cheats.h"
#include "memory.h"
#include "net/local_server.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Cheats {
std::atomic<bool> g_active{false};
std::atomic<bool> g_used{false};
}
namespace LocalOnlineServer { extern Mode g_testMode; extern bool g_testServer; }

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), std::streamsize(text.size()));
    Check(bool(out), "write fixture");
}
constexpr std::string_view kMap =
    "OCEANTRI-GECKO-MAP 1\nsource RMHE08\ntarget RMHP08\n"
    "map 8065AA60 8065CE60 1\nmap 8065AA62 8065CE62 2\nmap 8065AA64 8065CE64 4\n";
constexpr std::string_view kText = "$Mapped writes [test]\n0065AA60 0000007F\n0265AA62 00001234\n0465AA64 ABCDEF01\n";
}

int main() {
    try {
        Memory::Init(Memory::kMem1Size);
        CustomCodes::Code code;
        auto result = CustomCodes::Parse(kText, kMap, "fallback", code);
        Check(result.ok, result.message.c_str());
        Check(code.instructions.size() == 3 && code.sourceGame == "RMHE08", "three mapped widths");
        Check(code.instructions[0].source == 0x8065AA60 && code.instructions[0].target == 0x8065CE60, "explicit source/PAL mapping, no inferred offset");
        Check(code.instructions[2].value == 0xABCDEF01, "word value");
        CustomCodes::Code again;
        Check(CustomCodes::Parse(kText, kMap, "different-file-name", again).ok && again.id == code.id, "stable identity independent of filename");

        Check(!CustomCodes::Parse("0065AA60 00000001\n", "", "x", code).ok, "unmapped code rejected");
        Check(!CustomCodes::Parse("0465AA60 ABCDEF01\n", kMap, "x", code).ok, "wrong width mapping rejected");
        Check(!CustomCodes::Parse("0065AA60 0001007F\n", kMap, "x", code).ok, "repeat byte write rejected");
        Check(!CustomCodes::Parse("0265AA62 00011234\n", kMap, "x", code).ok, "repeat halfword write rejected");
        Check(!CustomCodes::Parse("0665AA60 00000001\n", kMap, "x", code).ok, "string/ASM-family write rejected");
        Check(!CustomCodes::Parse("2865AA60 00000001\n", kMap, "x", code).ok, "conditional rejected");
        Check(!CustomCodes::Parse("48000000 80000000\n", kMap, "x", code).ok, "base-address operation rejected");
        Check(!CustomCodes::Parse("C0000000 00000001\n", kMap, "x", code).ok, "ASM rejected");
        Check(!CustomCodes::Parse(kText, std::string(kMap) + "map 8065AA70 80001000 4\n", "x", code).ok, "executable target rejected");
        Check(!CustomCodes::Parse(kText, std::string(kMap) + "map 8065AA70 CC008000 4\n", "x", code).ok, "MMIO target rejected");
        Check(!CustomCodes::Parse(kText, std::string(kMap) + "map 8065AA70 8065CE65 4\n", "x", code).ok, "unaligned target rejected");
        auto wrongTarget = std::string(kMap);
        wrongTarget.replace(wrongTarget.find("target RMHP08"), 13, "target RMHE08");
        Check(!CustomCodes::Parse(kText, wrongTarget, "x", code).ok, "non-PAL target rejected");

        const auto root = std::filesystem::temp_directory_path() / ("oceantri-custom-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Write(root / "cheats" / "gecko" / "writes.txt", kText);
        Write(root / "cheats" / "gecko" / "writes.map", kMap);
        Write(root / "cheats" / "gecko" / "bad.txt", "C0000000 00000001\n");
        Write(root / "cheats" / "gecko" / "bad.map", kMap);
        CustomCodes::ResetForTests();
        CustomCodes::Initialize(root);
        auto view = CustomCodes::GetView();
        Check(view.codes.size() == 1 && view.enabled.size() == 1 && !view.enabled[0], "invalid package isolated; valid package starts OFF");
        Check(view.status.find("rejected") != std::string::npos, "rejected package diagnostic");
        const auto id = view.codes[0].id;
        Check(CustomCodes::QueueToggle(id, true), "queue custom toggle");
        Check(!CustomCodes::GetView().enabled[0], "UI does not mutate active custom state");
        CustomCodes::Pump();
        Check(CustomCodes::GetView().enabled[0], "game-thread pump applies custom toggle");
        Memory::Write32(0x8065CE60, 0);
        Memory::Write32(0x8065CE64, 0);
        CustomCodes::Step(nullptr);
        Check(Memory::Read32(0x8065CE60) == 0, "master OFF gates imported writes");
        Cheats::g_active.store(true);
        LocalOnlineServer::g_testMode = LocalOnlineServer::Mode::Community; // configured server, not an active session
        CustomCodes::Step(nullptr);
        Check(Memory::Read8(0x8065CE60) == 0x7F && Memory::Read16(0x8065CE62) == 0x1234 && Memory::Read32(0x8065CE64) == 0xABCDEF01, "mapped big-endian RAM writes");
        Check(Cheats::g_used.load(), "actual custom use blocks later online admission");
        Memory::Write32(0x8065CE64, 0x11111111);
        CustomCodes::Step(nullptr);
        Check(Memory::Read32(0x8065CE64) == 0xABCDEF01, "live refill each step");
        Memory::Write8(0x8065AA84u, 2); // guest City/online mode
        Memory::Write32(0x8065CE64, 0x22222222);
        CustomCodes::Step(nullptr);
        Check(Memory::Read32(0x8065CE64) == 0x22222222, "guest online mode gates custom writes");
        Memory::Write8(0x8065AA84u, 0);
        LocalOnlineServer::g_testServer = true;
        CustomCodes::Step(nullptr);
        Check(Memory::Read32(0x8065CE64) == 0x22222222, "local hosting gates custom writes");
        LocalOnlineServer::g_testServer = false;
        Check(CustomCodes::QueueToggle(id, false), "queue off");
        CustomCodes::Pump();
        CustomCodes::Step(nullptr);
        Check(Memory::Read32(0x8065CE64) == 0x22222222, "OFF stops writes without pretending to restore RAM");

        Check(CustomCodes::QueueToggle(id, true), "re-enable for persistence");
        CustomCodes::Pump();
        CustomCodes::Refresh();
        Check(CustomCodes::GetView().enabled[0], "custom switches survive rescan");
        CustomCodes::ApplyEnabled({});
        CustomCodes::Pump();
        Check(CustomCodes::EnabledIds().empty(), "disable all clears custom selection");
        Check(!CustomCodes::QueueToggle(123, true), "unknown custom ID rejected");
        CustomCodes::ResetForTests();
        std::filesystem::remove_all(root);
        std::cout << "Custom-code tests passed (strict parser, explicit mappings, endian writes, gates, persistence).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
