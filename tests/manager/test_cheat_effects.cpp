#include "cheats.h"
#include "launcher_cheats.h"
#include "memory.h"
#include "monster_control.h"
#include "port_check.h"
#include "game_step.h"
#include "step_counter.h"
#include "recomp_mod_loader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

extern "C" {
void MH3_MonsterHealthAdd(CpuContext*);
void MH3_CheatEquipData(CpuContext*);
void MH3_CheatSharpness(CpuContext*);
void MH3_CheatPlayerItemUse(CpuContext*);
void MH3_CheatBoxSlotCount(CpuContext*);
}

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
CpuContext cpu{};
int g_itemUseDelta = 0;
int g_sharpDelta = 0;
uint32_t g_equipData = 0;
unsigned g_itemAdds = 0, g_equipAdds = 0, g_talismanAdds = 0, g_guides = 0;

constexpr uint32_t kSystem = 0x8065AA60;
constexpr uint32_t kTable = 0x80750000, kPlayer = 0x80760000, kSave = 0x80770000;
constexpr uint32_t kMonster = 0x80780000, kOut = 0x80790000, kData = 0x80790100;

void SetupPlayer() {
    Memory::Write32(kSystem + 0xA4, kTable);
    Memory::Write16(kTable + 4, 1); // Kind::Player count
    Memory::Write32(kTable + 0x18, kPlayer);
    Memory::Write8(kSystem + 0x27, 0);
    Memory::Write16(kPlayer, 0x0101);
    Memory::Write32(kSystem + 0x95C, kSave);
    Memory::Write32(0x80796D40, kSave);
    PpcThread::state[PpcThread::Game].context = &cpu;
    cpu.gpr[1] = 0x807F0000;
}

// Guest callees used by the one-shot box fills. They record orchestration;
// they do not claim to reproduce the entire original save-file allocator.
void Guest_ItemObtainedSet(CpuContext*) { ++g_guides; }
void Guest_ItemBoxSlots(CpuContext* c) { c->gpr[3] = 800; }
void Guest_ItemBoxAdd(CpuContext*) { ++g_itemAdds; }
void Guest_EquipBoxSlots(CpuContext* c) { c->gpr[3] = 800; }
void Guest_EquipBoxRemove(CpuContext* c) { c->gpr[3] = 1; }
void Guest_EquipBoxAdd(CpuContext* c) { ++g_equipAdds; c->gpr[3] = 0; }
void Guest_EquipRarity(CpuContext* c) { c->gpr[3] = 1; }
void Guest_TalismanToBox(CpuContext*) { ++g_talismanAdds; }
void Guest_Index(CpuContext* c) { c->gpr[3] = 0; }

std::unordered_map<uint32_t, RawDispatchRecord> g_guest{
    {0x8004D318, {0x8004D318, Guest_ItemObtainedSet, 0}},
    {0x8004AED4, {0x8004AED4, Guest_ItemBoxSlots, 0}},
    {0x8004BF08, {0x8004BF08, Guest_ItemBoxAdd, 0}},
    {0x8004AEFC, {0x8004AEFC, Guest_EquipBoxSlots, 0}},
    {0x8004C744, {0x8004C744, Guest_EquipBoxRemove, 0}},
    {0x8004C578, {0x8004C578, Guest_EquipBoxAdd, 0}},
    {0x8027FDE0, {0x8027FDE0, Guest_EquipRarity, 0}},
    {0x8004C620, {0x8004C620, Guest_TalismanToBox, 0}},
    {0x8004D660, {0x8004D660, Guest_Index, 0}},
};
} // namespace

// Strong fixtures for the translated callees whose arguments the wrappers change.
extern "C" void func_80272E94(CpuContext* c) { g_itemUseDelta = int16_t(c->gpr[5]); }
extern "C" void func_802786D8(CpuContext* c) { g_sharpDelta = int16_t(c->gpr[4]); }
extern "C" void func_8027EE6C(CpuContext* c) { Memory::Write32(c->gpr[4], g_equipData); c->gpr[3] = 0; }
extern "C" void func_801EDA48(CpuContext*) {}

// Minimal registry/dispatch surface. No generated Wii code is linked or run.
void TranslatedFunctionRegistry::Register(TranslatedFunctionInfo) {}
void TranslatedFunctionRegistry::Finalize() { lookupPublished_.store(true, std::memory_order_release); }
const RawDispatchRecord* TranslatedFunctionRegistry::FindRawByAddressPtrSlow(uint32_t address) {
    const auto it = g_guest.find(address);
    return it == g_guest.end() ? nullptr : &it->second;
}
const TranslatedFunctionInfo* TranslatedFunctionRegistry::FindByAddressPtrSlow(uint32_t) { return nullptr; }
std::optional<TranslatedFunctionInfo> TranslatedFunctionRegistry::FindNearestByAddress(uint32_t) { return std::nullopt; }
std::optional<TranslatedFunctionInfo> TranslatedFunctionRegistry::FindByHostAddress(uintptr_t) { return std::nullopt; }
CpuContext& GetPersistentCpuContext() { return cpu; }

namespace RecompMod {
bool TryDispatchRsoOverlay(uint32_t, CpuContext*) { return false; }
void CheckRsoExecution(uint32_t) {}
}
namespace RuntimeCrash { [[noreturn]] void FatalMissingGuestTarget(uint32_t, CpuContext*) noexcept { std::abort(); } }
namespace MonsterControl { bool Frozen(uint32_t) { return false; } }
namespace GameStep { double Scale() { return 1.0; } uint32_t Ticks() { return 1; } }
namespace StepCounter { bool Every(uint32_t, uint32_t) { return false; } }
namespace Cheats { bool SlowMotion() { return false; } void StepMovementCodes() {} }
namespace PlayerHealth { void SetNoDamage(bool) {} }
namespace LauncherCheats { void SetNoStaminaDrain(bool) {} }
namespace PortCheck {
bool SetSlow() { return false; }
bool EnabledSlow(const char*) { return false; }
bool OriginalsSlow() { return false; }
void Run(CpuContext*, const char*, void (*)(CpuContext*), void (*)(CpuContext*)) {}
void RunPure(CpuContext*, const char*, Returns, void (*)(CpuContext*), void (*)(CpuContext*)) {}
void RunOutput(CpuContext*, const char*, std::initializer_list<Region>, void (*)(CpuContext*), void (*)(CpuContext*)) {}
void RunOutput(CpuContext*, const char*, Returns, std::initializer_list<Region>, void (*)(CpuContext*), void (*)(CpuContext*)) {}
bool RunPreciseSlow(const char*, uint32_t, bool, Returns, void (*)(CpuContext*), void (*)(CpuContext*)) { return false; }
}

int main() {
    try {
        Memory::Init(Memory::kMem1Size);
        SetupPlayer();
        TranslatedFunctionRegistry::Finalize();
        Cheats::SetActive(true);

        // Player per-step fields use the real StepPlayer path and PAL offsets.
        Memory::Write16(kPlayer + 0x370, 10); Memory::Write16(kPlayer + 0x372, 100); Memory::Write16(kPlayer + 0x376, 0);
        Memory::Write16(kPlayer + 0x378, 10); Memory::Write16(kPlayer + 0x37A, 100);
        Memory::Write8(kPlayer + 0x37F, 10); Memory::Write8(kPlayer + 0x381, 100);
        Memory::Write16(kPlayer + 0x56C, 10); Memory::Write16(kPlayer + 0x56E, 100);
        for (Cheats::Id id : {Cheats::Id::InfiniteHealth, Cheats::Id::InfiniteStamina, Cheats::Id::InfiniteAir, Cheats::Id::WeaponsSharp})
            LauncherCheats::Enable(size_t(id), true);
        Cheats::StepPlayer();
        Check(Memory::Read16(kPlayer + 0x370) == 100 && Memory::Read16(kPlayer + 0x376) == 100, "infinite health");
        Check(Memory::Read16(kPlayer + 0x378) == 100, "infinite stamina");
        Check(Memory::Read8(kPlayer + 0x37F) == 100, "infinite oxygen");
        Check(Memory::Read16(kPlayer + 0x56C) == 100, "infinite sharpness immediate refill");
        for (Cheats::Id id : {Cheats::Id::InfiniteHealth, Cheats::Id::InfiniteStamina, Cheats::Id::InfiniteAir, Cheats::Id::WeaponsSharp})
            LauncherCheats::Enable(size_t(id), false);

        // Consumables and ammunition are argument changes at the real use port.
        cpu.gpr[3] = kPlayer; cpu.gpr[5] = uint32_t(int32_t(-1)); cpu.lr = 0x80240000;
        LauncherCheats::Enable(size_t(Cheats::Id::UnlimitedConsumables), true);
        MH3_CheatPlayerItemUse(&cpu);
        Check(g_itemUseDelta == 0, "unlimited consumables");
        LauncherCheats::Enable(size_t(Cheats::Id::UnlimitedConsumables), false);
        cpu.gpr[5] = uint32_t(int32_t(-1));
        MH3_CheatPlayerItemUse(&cpu);
        Check(g_itemUseDelta == -1, "consumables restored when off");
        LauncherCheats::Enable(size_t(Cheats::Id::InfiniteAmmo), true);
        cpu.gpr[5] = uint32_t(int32_t(-1)); cpu.lr = 0x80287E5C;
        MH3_CheatPlayerItemUse(&cpu);
        Check(g_itemUseDelta == 0, "unlimited ammunition");
        LauncherCheats::Enable(size_t(Cheats::Id::InfiniteAmmo), false);

        // Box quantity read hook.
        Memory::Write16(kSave + 0x180, 1); Memory::Write16(kSave + 0x182, 1);
        LauncherCheats::Enable(size_t(Cheats::Id::BoxItemsX99), true);
        cpu.gpr[3] = 0;
        MH3_CheatBoxSlotCount(&cpu);
        Check(Memory::Read16(kSave + 0x182) == 99, "box quantities x99");
        LauncherCheats::Enable(size_t(Cheats::Id::BoxItemsX99), false);

        // One-hit kills bypass the capture floor only for a damaging hit.
        Memory::Write32(kMonster + 0x7A0, 100); Memory::Write32(kMonster + 0x7A4, 100);
        Memory::WriteFloat32(kMonster + 0x7B0, 0.5);
        LauncherCheats::Enable(size_t(Cheats::Id::OneHitKills), true);
        cpu.gpr[3] = kMonster; cpu.gpr[4] = uint32_t(int32_t(-1)); cpu.gpr[5] = 100;
        MH3_MonsterHealthAdd(&cpu);
        Check(Memory::Read32(kMonster + 0x7A0) == 0, "one-hit kill through capture floor");
        LauncherCheats::Enable(size_t(Cheats::Id::OneHitKills), false);
        Memory::Write32(kMonster + 0x7A0, 60);
        cpu.gpr[4] = uint32_t(int32_t(-20));
        MH3_MonsterHealthAdd(&cpu);
        Check(Memory::Read32(kMonster + 0x7A0) == 50, "original capture floor restored when off");

        // Maximum damage and sharpness-loss wrappers.
        g_equipData = kData;
        Memory::Write16(kData + 0xA, 100);
        LauncherCheats::Enable(size_t(Cheats::Id::MaximumDamage), true);
        cpu.gpr[3] = 0; cpu.gpr[4] = kOut; cpu.lr = 0x80273C34;
        MH3_CheatEquipData(&cpu);
        Check(Memory::Read16(kData + 0xA) == 32767, "maximum damage");
        LauncherCheats::Enable(size_t(Cheats::Id::MaximumDamage), false);
        Memory::Write16(kData + 0xA, 100);
        MH3_CheatEquipData(&cpu);
        Check(Memory::Read16(kData + 0xA) == 100, "damage restored when off");
        Memory::Write16(kPlayer + 0x56C, 10); Memory::Write16(kPlayer + 0x56E, 100);
        LauncherCheats::Enable(size_t(Cheats::Id::WeaponsSharp), true);
        cpu.gpr[3] = kPlayer; cpu.gpr[4] = uint32_t(int32_t(-5));
        MH3_CheatSharpness(&cpu);
        Check(g_sharpDelta == 90, "no sharpness loss");
        LauncherCheats::Enable(size_t(Cheats::Id::WeaponsSharp), false);

        // Money is a real save-field write in StepSaveData.
        Memory::Write32(kSave + 0x18, 1);
        LauncherCheats::Enable(size_t(Cheats::Id::MaxMoney), true);
        Cheats::StepSaveData();
        Check(Memory::Read32(kSave + 0x18) == 9999999, "maximum money");
        LauncherCheats::Enable(size_t(Cheats::Id::MaxMoney), false);

        // One-shot unlocks wait for a loaded character and call the real fill paths.
        Memory::Write32(0x806ADC90, 1);
        for (uint32_t kind = 1; kind <= 15; ++kind)
            Memory::Write32(0x80570E80 + 4 * (kind - 1), 2);
        const uint32_t skillList = 0x805C6C00;
        Memory::Write16(skillList, 1); Memory::Write16(skillList + 2, 0xFFFF);
        for (unsigned i = 0; i < 3; ++i)
            Memory::Write32(0x805C6B88 + 4 * i, skillList);
        LauncherCheats::Enable(size_t(Cheats::Id::AllItems), true);
        LauncherCheats::Enable(size_t(Cheats::Id::AllEquipment), true);
        Cheats::RequestBoxFill(false);
        Cheats::RequestBoxFill(true);
        Cheats::StepRequestedUnlocks(&cpu);
        Check(g_guides >= 2 && g_itemAdds > 700, "fill item box orchestration");
        Check(g_equipAdds > 0 && g_talismanAdds > 0, "unlock equipment orchestration");

        std::cout << "Native curated cheat hook tests passed against synthetic PAL guest RAM.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
