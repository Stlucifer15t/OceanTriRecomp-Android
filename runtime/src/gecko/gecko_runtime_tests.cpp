// Phase 2 checks: the real guest-memory adapter and the runtime manager.
// This target links Ocean's memory.cpp/guest_flat_memory.cpp and stubs the
// hardware services those files reference, so the adapter is exercised
// against the actual MEM1/MEM2 mapping rather than a fake buffer.

#include "gecko/gecko_manager.h"
#include "gecko/gecko_memory_guest.h"
#include "gecko/gecko_parser.h"
#include "gecko/gecko_storage.h"

#include "audio_interface.h"
#include "efb_host.h"
#include "gx_host.h"
#include "gx_panic.h"
#include "memory.h"
#include "recomp_mod_loader.h"
#include "video_interface.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// --- Hardware services referenced by memory.cpp ---------------------------

void ResetVideoInterface() {}
bool ReadVideoRegister16(uint32_t, uint16_t&) { return false; }
bool WriteVideoRegister16(uint32_t, uint16_t) { return false; }

void ResetAudioInterface() {}
bool ReadAudioRegister32(uint32_t, uint32_t&) { return false; }
bool WriteAudioRegister32(uint32_t, uint32_t) { return false; }

void ResetGxRegisters() {}
bool ReadGxRegister(uint32_t, unsigned, uint32_t&) { return false; }
bool WriteGxRegister(uint32_t, unsigned, uint32_t) { return false; }

bool ReadEfbColor32(uint32_t, uint32_t&) { return false; }
bool WriteEfbColor32(uint32_t, uint32_t) { return false; }

[[noreturn]] void PanicLegacyGx(const char*, uint32_t, const CpuContext*) noexcept {
    std::fprintf(stderr, "PanicLegacyGx called by the Gecko runtime test\n");
    std::abort();
}

namespace RecompMod {
uint32_t CurrentTranslatedExecutionAddress() noexcept {
    return 0;
}
} // namespace RecompMod

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

Gecko::Code ParseOne(const char* text) {
    const Gecko::ParseResult result = Gecko::Parse(text);

    if (result.codes.empty()) {
        Check(false, "runtime test: code parsed");
        return {};
    }

    return result.codes[0];
}

void TestGuestMemoryAdapter() {
    Gecko::GuestMemory memory;
    uint8_t b = 0;
    uint16_t h = 0;
    uint32_t w = 0;

    Check(memory.Valid(0x80000000u, 4), "guest memory: MEM1 base valid");
    Check(memory.Valid(0x817FFFFCu, 4), "guest memory: MEM1 last word valid");
    Check(memory.Valid(0x90000000u, 4), "guest memory: MEM2 base valid");
    Check(memory.Valid(0x93FFFFFCu, 4), "guest memory: MEM2 last word valid");
    Check(memory.Valid(0xC0000000u, 4), "guest memory: uncached MEM1 valid");
    Check(memory.Valid(0xD0000000u, 4), "guest memory: uncached MEM2 valid");

    Check(!memory.Valid(0x81800000u, 4), "guest memory: past MEM1 rejected");
    Check(!memory.Valid(0x94000000u, 4), "guest memory: past MEM2 rejected");
    Check(!memory.Valid(0x7FFFFFFCu, 4), "guest memory: below MEM1 rejected");
    Check(!memory.Valid(0x00000000u, 4), "guest memory: page zero rejected");
    Check(!memory.Valid(0xE0000000u, 4), "guest memory: locked cache rejected");
    Check(!memory.Valid(0xFFFFFFFFu, 4), "guest memory: address overflow rejected");
    Check(!memory.Valid(0x93FFFFFEu, 4), "guest memory: MEM2 end overflow rejected");

    Check(!memory.Valid(0x80000001u, 2), "guest memory: unaligned halfword rejected");
    Check(!memory.Valid(0x80000002u, 4), "guest memory: unaligned word rejected");
    Check(!memory.Write16(0x80000001u, 0x1234), "guest memory: unaligned write rejected");

    Check(memory.Write32(0x80000100u, 0xDEADBEEFu), "guest memory: MEM1 write");
    Check(memory.Read32(0x80000100u, w) && w == 0xDEADBEEFu, "guest memory: MEM1 read");
    Check(memory.Write16(0x80000104u, 0x1234u), "guest memory: MEM1 halfword write");
    Check(memory.Read16(0x80000104u, h) && h == 0x1234u, "guest memory: MEM1 halfword read");
    Check(memory.Write8(0x80000106u, 0x5Au), "guest memory: MEM1 byte write");
    Check(memory.Read8(0x80000106u, b) && b == 0x5Au, "guest memory: MEM1 byte read");

    Check(memory.Write32(0x90000100u, 0x12345678u), "guest memory: MEM2 write");
    Check(memory.Read32(0x90000100u, w) && w == 0x12345678u, "guest memory: MEM2 read");

    Check(!memory.Write32(0x81800000u, 1), "guest memory: out-of-range write rejected");
    Check(!memory.Read32(0x81800000u, w), "guest memory: out-of-range read rejected");
}

void TestManagerActivation() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);
    uint32_t w = 0;

    memory.Write32(0x80000100u, 0);
    memory.Write32(0x80000104u, 0);

    const Gecko::Code first = ParseOne("$First\n04000100 0000002A\n");
    const Gecko::Code second = ParseOne("$Second\n04000104 0000002B\n");

    manager.AddCode(first);
    manager.AddCode(second);
    manager.Step();

    Check(manager.EnabledCount() == 0, "manager: added codes start disabled");
    Check(memory.Read32(0x80000100u, w) && w == 0, "manager: disabled code does not run");

    manager.Enable(first.id, true);
    manager.Step();
    Check(manager.EnabledCount() == 1, "manager: one code enabled");
    Check(memory.Read32(0x80000100u, w) && w == 0x2Au, "manager: enabled code runs");
    Check(memory.Read32(0x80000104u, w) && w == 0, "manager: second code still disabled");

    manager.Enable(second.id, true);
    manager.Step();
    Check(manager.EnabledCount() == 2, "manager: two codes enabled");
    Check(memory.Read32(0x80000104u, w) && w == 0x2Bu, "manager: second code runs");

    const Gecko::ManagerSnapshot snapshot = manager.GetSnapshot();
    Check(snapshot.codes.size() == 2, "manager: snapshot code count");
    Check(snapshot.enabledCount == 2, "manager: snapshot enabled count");
    Check(snapshot.codes[0].enabled && snapshot.codes[1].enabled, "manager: snapshot flags");

    manager.Enable(first.id, false);
    manager.Step();
    Check(manager.EnabledCount() == 1, "manager: disable updates count");

    manager.Remove(second.id);
    manager.Step();
    Check(manager.GetSnapshot().codes.size() == 1, "manager: remove updates snapshot");
    Check(manager.EnabledCount() == 0, "manager: removed code no longer enabled");
}

void TestManagerStateLifecycle() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);
    uint32_t w = 0;

    memory.Write32(0x80000104u, 0);

    const Gecko::Code counter = ParseOne(
        "$Counter\n"
        "A8000000 00000002\n"
        "04000104 00000001\n"
        "E0000000 00000000\n");

    manager.AddCode(counter);
    manager.Enable(counter.id, true);

    manager.Step();
    Check(memory.Read32(0x80000104u, w) && w == 0, "manager: counter frame 1 skips");

    manager.Step();
    Check(memory.Read32(0x80000104u, w) && w == 1, "manager: counter frame 2 writes");

    Check(memory.Write32(0x80000104u, 0), "manager: counter memory reset");
    manager.Enable(counter.id, false);
    manager.Enable(counter.id, true);
    manager.Step();
    Check(memory.Read32(0x80000104u, w) && w == 0, "manager: disable resets counter state");

    manager.Step();
    Check(memory.Read32(0x80000104u, w) && w == 1, "manager: re-enabled counter restarts");

    Gecko::GuestMemory registerMemory;
    Gecko::Manager registerManager(registerMemory);

    const Gecko::Code persist = ParseOne(
        "$GrPersist\n"
        "A8000000 00000001\n"
        "80000005 00000010\n"
        "E2000001 00000000\n"
        "84000005 80000200\n"
        "E0000000 00000000\n");

    registerMemory.Write8(0x80000200u, 0);
    registerManager.AddCode(persist);
    registerManager.Enable(persist.id, true);

    registerManager.Step();
    uint8_t byteValue = 0;
    Check(registerMemory.Read8(0x80000200u, byteValue) && byteValue == 0x10, "manager: register set and stored frame 1");

    registerManager.Step();
    Check(registerMemory.Read8(0x80000200u, byteValue) && byteValue == 0x10, "manager: register persists across frames");
}

void TestOnlineGate() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);

    Check(manager.EnabledCount() == 0, "online gate: no codes enabled");

    const Gecko::Code code = ParseOne("$Gate\n04000100 00000001\n");

    manager.AddCode(code);
    manager.Step();
    Check(manager.EnabledCount() == 0, "online gate: registered but disabled is not counted");

    manager.Enable(code.id, true);
    manager.Step();
    Check(manager.EnabledCount() == 1, "online gate: enabled code counted");

    manager.Enable(code.id, false);
    manager.Step();
    Check(manager.EnabledCount() == 0, "online gate: disabling restores correct state");
}



void TestUnsupportedNeverExecutes() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);

    const Gecko::Code code = ParseOne(
        "$ASM\n"
        "C2000000 00000001\n"
        "60000000 00000000\n"
        "00000000 00000000\n");

    manager.AddCode(code);
    manager.Enable(code.id, true);
    manager.Step();

    Check(manager.EnabledCount() == 0, "hardening: unsupported code disabled");

    const Gecko::ManagerSnapshot snapshot = manager.GetSnapshot();
    Check(snapshot.codes.size() == 1, "hardening: unsupported snapshot count");
    Check(snapshot.codes.size() == 1 && !snapshot.codes[0].enabled, "hardening: unsupported not enabled");
    Check(snapshot.codes.size() == 1 && !snapshot.codes[0].runtimeDiagnostics.empty(), "hardening: unsupported diagnostic");
}

void TestPendingEnableOnlineGate() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);

    const Gecko::Code code = ParseOne("$Pending\n04000000 00000001\n");
    manager.AddCode(code);
    manager.Step();

    Check(manager.EnabledCount() == 0, "hardening: added code disabled");

    manager.Enable(code.id, true);
    Check(manager.EnabledCount() == 1, "hardening: pending enable counted");

    manager.Step();
    Check(manager.EnabledCount() == 1, "hardening: enabled code counted");
}

void TestStorageSizeLimit() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_large/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    {
        std::ofstream out(path, std::ios::binary);
        const std::string big(4u * 1024u * 1024u + 1u, 'x');
        out.write(big.data(), std::streamsize(big.size()));
    }

    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(!Gecko::LoadCodes(path, loaded, diagnostics), "hardening: oversized storage rejected");
    Check(!diagnostics.empty(), "hardening: oversized storage diagnostic");
}

void TestStorageRoundTrip() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_roundtrip/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());

    const Gecko::Code first = ParseOne("$First\n04000000 00000001\n");
    const Gecko::Code second = ParseOne("$Second\n02000000 00000002\n");
    const std::vector<Gecko::Code> codes{first, second};

    Check(Gecko::SaveCodes(path, codes), "storage: save round-trip");
    Check(std::filesystem::exists(path), "storage: codes file exists");

    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, loaded, diagnostics), "storage: load round-trip");
    Check(loaded.size() == 2, "storage: two codes loaded");
    Check(diagnostics.empty(), "storage: no diagnostics");
    Check(loaded.size() == 2 && loaded[0].name == "First", "storage: first name");
    Check(loaded.size() == 2 && loaded[1].name == "Second", "storage: second name");
    Check(loaded.size() == 2 && loaded[0].id == first.id, "storage: first id");
    Check(loaded.size() == 2 && loaded[0].sourceText.find("$First") != std::string::npos, "storage: source text");
}

void TestStorageEmpty() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_empty/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());

    Check(Gecko::SaveCodes(path, {}), "storage: save empty");
    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, loaded, diagnostics), "storage: load empty");
    Check(loaded.empty(), "storage: empty collection");
    Check(diagnostics.empty(), "storage: empty diagnostics");
}

void TestStorageMalformed() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_malformed/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    {
        std::ofstream out(path);
        out << "this is not a Gecko file\n";
    }

    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, loaded, diagnostics), "storage: malformed load returns");
    Check(loaded.empty(), "storage: malformed has no codes");
    Check(!diagnostics.empty(), "storage: malformed diagnostic");
    Check(std::filesystem::exists(path + ".corrupt") || std::filesystem::exists(path + ".bak"), "storage: malformed original preserved");
}

void TestStorageDuplicates() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_duplicates/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    {
        std::ofstream out(path);
        out << "$Same\n04000000 00000001\n$Same\n04000000 00000001\n";
    }

    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, loaded, diagnostics), "storage: duplicate load");
    Check(loaded.size() == 1, "storage: duplicate deduped");
}

void TestStorageAtomicFailure() {
    const std::string dir = "/tmp/mh3_gecko_runtime_test/storage_failure";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    {
        std::ofstream blocker(dir + "/notadir");
        blocker << "x";
    }

    const Gecko::Code code = ParseOne("$Code\n04000000 00000001\n");
    Check(!Gecko::SaveCodes(dir + "/notadir/codes.txt", {code}), "storage: atomic write failure reported");
}

void TestStorageMetadata() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/storage_metadata/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());

    const Gecko::Code code = ParseOne("RMHE08\n$Code\n04000000 00000000\n");
    Check(code.gameId == "RMHE08", "storage: metadata source game id");
    Check(code.gameCompatibility == Gecko::GameCompatibility::RegionMismatch, "storage: metadata source compatibility");

    Check(Gecko::SaveCodes(path, {code}), "storage: metadata save");

    std::vector<Gecko::Code> loaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, loaded, diagnostics), "storage: metadata load");
    Check(loaded.size() == 1, "storage: metadata code count");
    Check(loaded.size() == 1 && loaded[0].gameId == "RMHE08", "storage: metadata game id preserved");
    Check(loaded.size() == 1 && loaded[0].region == "US", "storage: metadata region preserved");
    Check(loaded.size() == 1 && loaded[0].gameCompatibility == Gecko::GameCompatibility::RegionMismatch, "storage: metadata compatibility preserved");
}

void TestImportWorkflow() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/import_flow/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());

    const std::string fileText = "$Alpha\n04000000 00000001\n$Beta\n02000000 00000002\n";
    Check(Gecko::SaveImportedFile("test_import.txt", fileText), "import: raw file saved");
    Check(std::filesystem::exists(Gecko::ImportsDir() + "/test_import.txt"), "import: raw file exists");

    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);
    manager.SetStoragePath(path);

    const Gecko::ParseResult parsed = Gecko::Parse(fileText);
    Check(parsed.codes.size() == 2, "import: two codes parsed");

    for (const Gecko::Code& code : parsed.codes)
        manager.AddCode(code);

    manager.Step();
    manager.LoadInitialCodes();

    const Gecko::ManagerSnapshot snapshot = manager.GetSnapshot();
    Check(snapshot.codes.size() == 2, "import: codes registered");
    Check(snapshot.enabledCount == 0, "import: registered codes disabled");

    std::vector<Gecko::Code> reloaded;
    std::vector<Gecko::Diagnostic> diagnostics;
    Check(Gecko::LoadCodes(path, reloaded, diagnostics), "import: storage loads");
    Check(reloaded.size() == 2, "import: two codes persisted");
}

void TestManagerRestart() {
    const std::string path = "/tmp/mh3_gecko_runtime_test/manager_restart/codes.txt";
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());

    Gecko::GuestMemory memory;
    Gecko::Manager first(memory);
    first.SetStoragePath(path);

    const Gecko::Code code = ParseOne("$Persisted\n04000100 0000002A\n");
    first.AddCode(code);
    first.Step();

    Gecko::Manager second(memory);
    second.SetStoragePath(path);
    second.LoadInitialCodes();

    const Gecko::ManagerSnapshot snapshot = second.GetSnapshot();
    Check(snapshot.codes.size() == 1, "storage: restart loads code");
    Check(snapshot.codes.size() == 1 && snapshot.codes[0].name == "Persisted", "storage: restart name");
    Check(snapshot.codes.size() == 1 && !snapshot.codes[0].enabled, "storage: restart code disabled");
}

void TestManagerFaultHandling() {
    Gecko::GuestMemory memory;
    Gecko::Manager manager(memory);

    const Gecko::Code bad = ParseOne("$Bad\n05800000 00000001\n");

    manager.AddCode(bad);
    manager.Enable(bad.id, true);
    manager.SetAutoDisableAfterErrors(2);

    manager.Step();
    Check(manager.EnabledCount() == 1, "manager: faulting code still enabled after one error");

    manager.Step();
    Check(manager.EnabledCount() == 0, "manager: faulting code auto-disabled");

    const Gecko::ManagerSnapshot snapshot = manager.GetSnapshot();
    Check(snapshot.codes.size() == 1, "manager: fault snapshot count");
    Check(!snapshot.codes[0].enabled, "manager: fault snapshot disabled");
    Check(!snapshot.codes[0].runtimeDiagnostics.empty(), "manager: fault diagnostic recorded");
}

} // namespace

int main() {
    const std::string testDir = "/tmp/mh3_gecko_runtime_test";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);
    setenv("MH3_USER_DIR", testDir.c_str(), 1);

    ::Memory::Init(::Memory::kMem1Size);

    TestGuestMemoryAdapter();
    TestManagerActivation();
    TestManagerStateLifecycle();
    TestOnlineGate();
    TestManagerFaultHandling();
    TestUnsupportedNeverExecutes();
    TestPendingEnableOnlineGate();
    TestStorageSizeLimit();
    TestStorageRoundTrip();
    TestStorageEmpty();
    TestStorageMalformed();
    TestStorageDuplicates();
    TestStorageAtomicFailure();
    TestStorageMetadata();
    TestImportWorkflow();
    TestManagerRestart();

    if (failures != 0) {
        std::cerr << failures << " Gecko runtime check(s) failed\n";
        return 1;
    }

    std::cout << "Gecko runtime checks passed\n";
    std::filesystem::remove_all(testDir);
    return 0;
}
