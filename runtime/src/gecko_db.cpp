// The bundled Gecko code library (gecko_db.h).
//
// One entry per cheat of runtime/src/cheats.cpp: the Gecko tab shows the same
// set of changes the native cheat list does, so nothing the app can do is
// missing from it.
//
// Two shapes, and the difference matters:
//
//   1. A data code (`body` not empty). The port's whole implementation is a
//      write of a fixed value to a fixed field, so the same write is expressed
//      in Gecko's own encoding and this runtime executes it once per logic step.
//      The character's save data is reached through Gecko's pointer forms (48
//      loads po from the pointer variable, 14/15/10 write through it), because
//      the game moves that data and a code must follow it.
//   2. A native-backed entry (`body` empty). The published code hooks or patches
//      a game function, needs a value the player picks, or follows a pointer the
//      code handler's operand flags would have to chase (this engine refuses
//      those flags rather than guess at an address). A static recompilation
//      cannot run such a code anyway - the ARM64 functions were made at build
//      time - so the built-in port is the implementation and `native` names it.
//      The manager offers that instead of a switch that would do nothing.
//
// Addresses are this disc's (PAL, RMHP08), as runtime/src/cheat_*.cpp documents
// them.
#include "gecko_db.h"

namespace Gecko {
namespace {

constexpr uint32_t kSystem = 0x8065AA60;          // system_w
constexpr uint32_t kUserDataPointer = 0x80796D40; // holds the save data's address

// A word write (04XXXXXX ZZZZZZZZ, or 14 through po).
Line Word(uint32_t address, uint32_t value, bool throughPointer = false) {
    return Line{(throughPointer ? 0x14000000u : 0x04000000u) | (address & 0x01FFFFFFu), value};
}

// An 8-bit write of one byte (00XXXXXX 000000ZZ, or 10 through po).
Line Byte(uint32_t address, uint8_t value, bool throughPointer = false) {
    return Line{(throughPointer ? 0x10000000u : 0x00000000u) | (address & 0x01FFFFFFu), value};
}

// A 16-bit write of one halfword (02XXXXXX 0001ZZZZ: one write, or 12 through
// po).
Line Half(uint32_t address, uint16_t value, bool throughPointer = false) {
    return Line{(throughPointer ? 0x12000000u : 0x02000000u) | (address & 0x01FFFFFFu),
                uint32_t(0x00010000u | value)};
}

// po = [0x80796D40]: the character's save data, through the pointer variable the
// game keeps it in.
constexpr Line SaveData() {
    return Line{0x48000000u, 0x80796D40u};
}

} // namespace

std::span<const DbCode> DbCodes() {
    static const std::vector<DbCode> codes{

        // ---------------------------------------------------------------
        // Player
        // ---------------------------------------------------------------
        {"No damage", "Player", {},
         "NoDamage",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No action stamina drain", "Player", {},
         "NoStaminaDrain",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felvine underwater", "Player", {},
         "FelvineUnderwater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Show real element", "Player", {},
         "ShowRealElement",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Show real status", "Player", {},
         "ShowRealStatus",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Alternate infinite health", "Player", {},
         "AltInfiniteHealth",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Increase oxygen bar (online code)", "Player", {},
         "SethOxygen",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Health never changes", "Player", {},
         "HealthNeverChanges",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always able to sleep", "Player", {},
         "AlwaysAbleToSleep",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Ghost world", "Player", {},
         "GhostWorld",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Change to morning (upon loading)", "Player", {},
         "MorningOnLoad",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Change to evening (upon loading)", "Player", {},
         "EveningOnLoad",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never stunned", "Player", {},
         "NeverStunned",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No reload", "Player", {},
         "NoReload",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max res on converting", "Player", {},
         "MaxResourcesOnConverting",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Res 0", "Player",
         {SaveData(), Word(0x003F04u, 0x0, true)},
         "ResourcesZero",
         "The same write the native port makes, once per logic step."},
        {"Boss/s mod", "Player", {},
         "BossMod",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Set boss/s size to", "Player", {},
         "BossSize",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Set boss/s level to", "Player", {},
         "BossLevel",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Sex change", "Player",
         {SaveData(), Byte(0x000000u, 0x1, true)},
         "SexChange",
         "The same write the native port makes, once per logic step."},
        {"Infinite health", "Player", {},
         "InfiniteHealth",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite stamina", "Player", {},
         "InfiniteStamina",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max fire", "Player", {},
         "MaxFire",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max water", "Player", {},
         "MaxWater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max thunder", "Player", {},
         "MaxThunder",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max dragon", "Player", {},
         "MaxDragon",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max ice", "Player", {},
         "MaxIce",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never life drain", "Player", {},
         "NeverLifeDrain",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add 5 to dragon", "Player", {},
         "AddDragon",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add 5 to thunder", "Player", {},
         "AddThunder",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add 5 to ice", "Player", {},
         "AddIce",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add 5 to fire (effect)", "Player", {},
         "AddFire",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Adds 5 to water", "Player", {},
         "AddWater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No negative ice stats effect", "Player", {},
         "NoIceWeakness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No negative dragon stats effect", "Player", {},
         "NoDragonWeakness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No negative water stats effect", "Player", {},
         "NoWaterWeakness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No negative thunder stats effect", "Player", {},
         "NoThunderWeakness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No negative fire stats", "Player", {},
         "NoFireWeakness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never get felvine-scented", "Player", {},
         "NeverFelvine",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never get snowman", "Player", {},
         "NeverSnowman",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never get muddy", "Player", {},
         "NeverMuddy",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always windproof", "Player", {},
         "AlwaysWindproof",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never poison", "Player", {},
         "NeverPoison",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always have natural rec up", "Player", {},
         "NaturalRecoveryUp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Health bar flash purple", "Player", {},
         "PurpleHealthBar",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No element", "Player", {},
         "NoElementStatus",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Puddy face", "Player",
         {SaveData(), Byte(0x000001u, 0x1, true)},
         "PuddyFace",
         "The same write the native port makes, once per logic step."},
        {"Clothing change", "Player",
         {SaveData(), Byte(0x00009Eu, 0x1, true)},
         "Clothing",
         "The same write the native port makes, once per logic step."},
        {"Voice change", "Player",
         {SaveData(), Byte(0x00009Fu, 0x1, true)},
         "Voice",
         "The same write the native port makes, once per logic step."},
        {"Eye color change", "Player",
         {SaveData(), Byte(0x003DE6u, 0xFF, true)},
         "EyeColor",
         "The same write the native port makes, once per logic step."},
        {"Features", "Player",
         {SaveData(), Byte(0x003DE7u, 0x1, true)},
         "Features",
         "The same write the native port makes, once per logic step."},
        {"Color changing clothing (red)", "Player", {},
         "ClothingRed",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Color changing clothing (green)", "Player", {},
         "ClothingGreen",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Color changing clothing (blue)", "Player", {},
         "ClothingBlue",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},

        // ---------------------------------------------------------------
        // Items
        // ---------------------------------------------------------------
        {"All items in the item box", "Items", {},
         "AllItems",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"All equipment in the equipment box", "Items", {},
         "AllEquipment",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Can't carve/gather (on/off)", "Items", {},
         "CantGather",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Talismans sent to box start at slot", "Items", {},
         "TalismanStartSlot",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite bombs in pouch", "Items", {},
         "InfiniteBombsInPouch",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Set unlimited traps", "Items", {},
         "UnlimitedTraps",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Set unlimited barrel bombs", "Items", {},
         "UnlimitedBarrelBombs",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite items", "Items", {},
         "InfiniteItems",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No quest box jiggle", "Items", {},
         "NoQuestBoxJiggle",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"All boxes have 8 pages", "Items", {},
         "AllBoxes8Pages",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Item pouch items x10", "Items", {},
         "PouchItemsX10",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"All item box items x99", "Items", {},
         "BoxItemsX99",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Inf gathering", "Items", {},
         "InfiniteGathering",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Inf carving", "Items", {},
         "InfiniteCarving",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Gathering/carving mod", "Items", {},
         "GatherItem",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Item/equip boxes have 8 pages", "Items", {},
         "Boxes8Pages",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Gain/use items/ammo get 10", "Items", {},
         "ItemsGet10",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Sell/use item get 10", "Items", {},
         "SellUseGet10",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Buy new item get 10", "Items", {},
         "BuyNewGet10",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"All items cost 1z in item stores", "Items", {},
         "ItemsCost1z",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Dont lose money buying items", "Items", {},
         "DontLoseMoney",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Smithy forge/upgrade with 1 item", "Items", {},
         "SmithyOneItem",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Unlimited cold drink", "Items", {},
         "UnlimitedColdDrink",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Unlimited hot drink", "Items", {},
         "UnlimitedHotDrink",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Inf ammo", "Items", {},
         "InfiniteAmmo",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enable all ammo v2", "Items", {},
         "EnableAllAmmo",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Rapidfire all ammo", "Items", {},
         "RapidFire",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},

        // ---------------------------------------------------------------
        // Combat
        // ---------------------------------------------------------------
        {"Show all monsters on the map", "Combat", {},
         "AllMonstersOnMap",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Tranq attaq", "Combat", {},
         "TranqAttaq",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Monster HP multipler", "Combat", {},
         "MonsterHpMultiplier",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Monsters have regen during rage", "Combat", {},
         "RageRegen",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Instant rage mode", "Combat", {},
         "InstantRage",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite rage mode", "Combat", {},
         "InfiniteRage",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite monster stamina", "Combat", {},
         "InfiniteMonsterStamina",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Monster speed modifier", "Combat", {},
         "MonsterSpeedModifier",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max heal all monsters", "Combat", {},
         "MaxHealMonsters",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felvine lasts forever on monster", "Combat", {},
         "FelvineForever",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Monster can't see you", "Combat", {},
         "MonsterCantSeeYou",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Action repeater for targeted monster", "Combat", {},
         "MonsterActionRepeater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Freeze/unfreeze monsters", "Combat", {},
         "FreezeUnfreezeMonsters",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Weapons always sharp (best version)", "Combat", {},
         "SethWeaponsSharp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Weapons ultra sharp (online code)", "Combat", {},
         "SethUltraSharp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Combine never disabled", "Combat", {},
         "CombineEnabled",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Long swords max sharpness", "Combat", {},
         "LongSwordsMaxSharpness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Defeats counter always 0", "Combat", {},
         "NoDefeats",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Daredevil mode", "Combat", {},
         "Daredevil",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Crossbow rapid fire", "Combat", {},
         "CrossbowRapidFire",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No need to have required materials", "Combat", {},
         "NoRequiredMaterials",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite ressources", "Combat",
         {SaveData(), Word(0x003F04u, 0x5F5E0FF, true)},
         "InfiniteResources",
         "The same write the native port makes, once per logic step."},
        {"Character name change", "Combat", {},
         "ProfileNameModifier",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Action repeater", "Combat", {},
         "ActionRepeater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"1 hit kills", "Combat", {},
         "OneHitKills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enemy has 1 HP (good for capture/cutting parts off)", "Combat", {},
         "EnemyOneHp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Sharpness stays full", "Combat", {},
         "WeaponsSharp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"On hit long sword power max (charge bar)", "Combat", {},
         "LongSwordPowerMax",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Weapon max attack base (press buttons when on weapon)", "Combat", {},
         "MaxAttackBase",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max sharp (press buttons when on weapon)", "Combat", {},
         "MaxSharpness",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max affinity (press buttons when on weapon)", "Combat", {},
         "MaxAffinity",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Defense upgrade (255)", "Combat", {},
         "DefenseUpgrade",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Defense max (255)", "Combat", {},
         "DefenseMax",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Attack upgrade (255)", "Combat", {},
         "AttackUpgrade",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add 1000 to attack", "Combat", {},
         "Add1000Attack",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Never get defense down", "Combat", {},
         "NeverDefenseDown",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felyne skill 1 button activation", "Combat", {},
         "FelyneSkill1",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felyne skill 2 button activation", "Combat", {},
         "FelyneSkill2",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felyne skill 3 button activation", "Combat", {},
         "FelyneSkill3",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Felyne skill 4 button activation", "Combat", {},
         "FelyneSkill4",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Add to defense", "Combat", {},
         "AddToDefense",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced focus (improved weapon charge for GS, HA, LS, SW)", "Combat", {},
         "EnhancedFocus",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"GreatSword inf charge and Lv3 charge always", "Combat", {},
         "GreatSwordCharge",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hammer max charge always", "Combat", {},
         "HammerMaxCharge",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Lance charge speed increased", "Combat", {},
         "LanceChargeSpeed",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Lance charge speed controller", "Combat", {},
         "LanceChargeController",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (great sword)", "Combat", {},
         "AirAttackGreatSword",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (sword and shield)", "Combat", {},
         "AirAttackSwordAndShield",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (hammer)", "Combat", {},
         "AirAttackHammer",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (lance)", "Combat", {},
         "AirAttackLance",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (long sword)", "Combat", {},
         "AirAttackLongSword",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (switch axe)", "Combat", {},
         "AirAttackSwitchAxe",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Air attack (bowgun)", "Combat", {},
         "AirAttackBowgun",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Instant sharpen", "Combat", {},
         "InstantSharpen",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Each monster kill at hunt report", "Combat", {},
         "HuntReportKills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Exceed weapon size on attack", "Combat", {},
         "WeaponReach",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Pass through monsters (on/off)", "Combat", {},
         "PassThroughMonsters",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Freeze monsters", "Combat", {},
         "FreezeMonsters",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Activate armor skills for all helmet", "Combat", {},
         "HelmSkills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Activate armor skills for all chest", "Combat", {},
         "ChestSkills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Activate armor skills for all arms", "Combat", {},
         "ArmsSkills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Activate armor skills for all waist", "Combat", {},
         "WaistSkills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Activate armor skills for all legs", "Combat", {},
         "LegsSkills",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Deactivate armor skills codes (turn off)", "Combat", {},
         "ArmorSkillsOff",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},

        // ---------------------------------------------------------------
        // Progression
        // ---------------------------------------------------------------
        {"Unlock all gallery clips", "Progression",
         {Word(0x000888u, 0x1)},
         "GalleryClips",
         "The same write the native port makes, once per logic step."},
        {"Armor is upgradeable to high rank even at low rank", "Progression", {},
         "HighRankArmorUpgrades",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No HRP gain/loss", "Progression", {},
         "NoHrpChange",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equip blade/gunner armor", "Progression", {},
         "BothArmorTypes",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Speed combos never disabled", "Progression", {},
         "SpeedCombosEnabled",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Join/host any quest", "Progression", {},
         "JoinAnyQuest",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always display HR infos on status", "Progression", {},
         "AlwaysShowHrInfo",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Purchase any weapon with HR0", "Progression", {},
         "BuyAnyWeaponAtHr0",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunter ranking modifier", "Progression", {},
         "HunterRankModifier",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite time for choosing your equipment", "Progression", {},
         "ArenaEquipmentTime",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No need for money", "Progression", {},
         "NoNeedForMoney",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Unlock all star quests", "Progression",
         {SaveData(), Half(0x0047DAu, 0xFFFF, true)},
         "AllStarQuests",
         "The same write the native port makes, once per logic step."},
        {"All quests completed", "Progression",
         {SaveData(), Word(0x00512Cu, 0xFFFFFFFF, true)},
         "AllQuestsCompleted",
         "The same write the native port makes, once per logic step."},
        {"Infinite money", "Progression",
         {SaveData(), Word(0x000018u, 0x98967F, true)},
         "MaxMoney",
         "The same write the native port makes, once per logic step."},
        {"Set smithy req materials to", "Progression", {},
         "SmithyMaterial",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Infinite time arena and quests", "Progression", {},
         "InfiniteTime",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always face forward when using R stick", "Progression", {},
         "FaceForwardStick",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Always face forward when using left/right d-pad", "Progression", {},
         "FaceForwardDpad",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Faster weapon usage", "Progression", {},
         "FasterWeaponUsage",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"GreatSword no overcharging", "Progression", {},
         "NoOvercharging",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Sword and shield inf slashes", "Progression", {},
         "SnsInfiniteSlashes",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Battojutsu mode (long sword quick draw)", "Progression", {},
         "Battojutsu",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Force switch axe sword mode", "Progression", {},
         "SwitchAxeSwordMode",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Instant well-done steak", "Progression", {},
         "InstantSteak",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Force grill", "Progression", {},
         "ForceGrill",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enable all hunting fleets", "Progression", {},
         "AllHuntingFleets",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet captain always has catch", "Progression", {},
         "CaptainCatch",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet captain always 4 stars", "Progression", {},
         "CaptainFourStars",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet Redspear always has catch", "Progression", {},
         "RedspearCatch",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet Redspear always 4 stars", "Progression", {},
         "RedspearFourStars",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet Blackspear always has catch", "Progression", {},
         "BlackspearCatch",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hunting fleet Blackspear always 4 stars", "Progression", {},
         "BlackspearFourStars",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"All hunting fleets: half a day voyages", "Progression", {},
         "HalfDayVoyages",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"1 million zenny prize from Chamberlyne", "Progression",
         {SaveData(), Word(0x0053A4u, 0xF4240, true)},
         "ChamberlynePrize",
         "The same write the native port makes, once per logic step."},
        {"Enable all challenges in offline arena", "Progression",
         {SaveData(), Half(0x0052E2u, 0x3FF, true)},
         "AllArenaChallenges",
         "The same write the native port makes, once per logic step."},
        {"Enable all Felyne helpers", "Progression", {},
         "AllFelyneHelpers",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Max out all farm stats", "Progression",
         {SaveData(), Word(0x004654u, 0x3030303, true)},
         "MaxFarmStats",
         "The same write the native port makes, once per logic step."},
        {"Instant farming for all Felyne helpers", "Progression", {},
         "InstantFarming",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Boss 1 Moga forecast tomorrow", "Progression", {},
         "ForecastTomorrow1",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Boss 2 Moga forecast tomorrow", "Progression", {},
         "ForecastTomorrow2",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Boss 1 Moga forecast day after", "Progression", {},
         "ForecastDayAfter1",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Boss 2 Moga forecast day after", "Progression", {},
         "ForecastDayAfter2",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Combo list complete", "Progression", {},
         "ComboListComplete",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Quick gestures", "Progression", {},
         "QuickGestures",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Force sheathe weapon", "Progression", {},
         "ForceSheathe",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Force unsheathe weapon", "Progression", {},
         "ForceUnsheathe",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Force evade (quick recover)", "Progression", {},
         "ForceEvade",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Sleep anywhere", "Progression", {},
         "SleepAnywhere",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Thrown back to camp", "Progression", {},
         "BackToCamp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced movement speed (quest/village)", "Progression", {},
         "EnhancedMovement",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced dashing (quest/village)", "Progression", {},
         "EnhancedDashing",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced evade distances", "Progression", {},
         "EnhancedEvade",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Evade distance x2 (needs Enhanced evade distances)", "Progression", {},
         "EvadeDistanceX2",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Evade distance x10 (needs Enhanced evade distances)", "Progression", {},
         "EvadeDistanceX10",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Walk through walls (on/off)", "Progression", {},
         "WalkThroughWalls",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Slow-mo mode", "Progression", {},
         "SlowMo",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Switch axe power gauge always full", "Progression", {},
         "SwitchAxeGaugeFull",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Atk up durations have no limit", "Progression", {},
         "AttackUpForever",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Def up durations have no limit", "Progression", {},
         "DefenseUpForever",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equipped helm level 255", "Progression", {},
         "HelmLevel255",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equipped vest level 255", "Progression", {},
         "VestLevel255",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equipped gloves level 255 (quest only)", "Progression", {},
         "GlovesLevel255",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equipped belt level 255 (quest only)", "Progression", {},
         "BeltLevel255",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Equipped pants level 255 (quest only)", "Progression", {},
         "PantsLevel255",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Skin tone change", "Progression", {},
         "SkinTone",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Range for all bowgun barrels", "Progression", {},
         "BarrelRange",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},

        // ---------------------------------------------------------------
        // World
        // ---------------------------------------------------------------
        {"Turn it up to 11", "World", {},
         "TurnItUpTo11",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Language changer", "World", {},
         "LanguageChanger",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Instant flex", "World", {},
         "InstantFlex",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Can't save", "World", {},
         "CantSave",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Don't save new arena records", "World", {},
         "NoArenaRecords",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enable armor pigment for all armor", "World", {},
         "ArmorPigment",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Can't leave area (on/off)", "World", {},
         "CantLeaveArea",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Farcaster at camp", "World", {},
         "FarcasterAtCamp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Shock at camp", "World", {},
         "ShockAtCamp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"LBB at camp", "World", {},
         "BarrelBombAtCamp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Smoke underwater", "World", {},
         "SmokeUnderwater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Steak underwater", "World", {},
         "SteakUnderwater",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Farcaster in phase 2", "World", {},
         "FarcasterPhase2",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hold to hide rewards", "World", {},
         "HideRewards",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Show real raw", "World", {},
         "ShowRealRaw",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Map attached to HUD", "World", {},
         "MapOnHud",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Inf retries + reward increased instead", "World", {},
         "InfiniteRetries",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Autofail", "World", {},
         "Autofail",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Autoclear (main and subs)", "World", {},
         "Autoclear",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Cursed weapons", "World", {},
         "CursedWeapons",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No knockback", "World", {},
         "NoKnockback",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"No playtime increase", "World", {},
         "NoPlaytimeIncrease",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Set playtime", "World", {},
         "SetPlayTime",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Load XX shots", "World", {},
         "LoadShots",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},

        // ---------------------------------------------------------------
        // Movement
        // ---------------------------------------------------------------
        {"Pitfall at camp", "Movement", {},
         "PitfallAtCamp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Speed hack in village", "Movement", {},
         "VillageSpeedHack",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Snail speed (village only)", "Movement", {},
         "VillageSnailSpeed",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Speed glide/skate (online code)", "Movement", {},
         "SethSpeedGlide",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Super speed", "Movement", {},
         "SuperSpeed",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Moon jump", "Movement", {},
         "MoonJump",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Teleporter", "Movement", {},
         "Teleporter",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Free fly", "Movement", {},
         "FreeFly",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Inf air", "Movement", {},
         "InfiniteAir",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Speed up when running", "Movement", {},
         "RunSpeedUp",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Hair dresser", "Movement",
         {SaveData(), Byte(0x000002u, 0x1, true)},
         "HairStyle",
         "The same write the native port makes, once per logic step."},
        {"Hair dye (red)", "Movement",
         {SaveData(), Byte(0x000014u, 0xFF, true)},
         "HairRed",
         "The same write the native port makes, once per logic step."},
        {"Hair dye (green)", "Movement",
         {SaveData(), Byte(0x000015u, 0xFF, true)},
         "HairGreen",
         "The same write the native port makes, once per logic step."},
        {"Hair dye (blue)", "Movement",
         {SaveData(), Byte(0x000016u, 0xFF, true)},
         "HairBlue",
         "The same write the native port makes, once per logic step."},
        {"Cancel camera zoom-in when falling", "Movement", {},
         "NoFallZoom",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced leap at will (press dash and action button)", "Movement", {},
         "EnhancedLeap",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Jump at will (press the action button to jump)", "Movement", {},
         "JumpAtWill",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Jump very high (press dash and action button)", "Movement", {},
         "JumpVeryHigh",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Moonjump (A button)", "Movement", {},
         "DoudleyMoonJump",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Glider (single play)", "Movement", {},
         "Glider",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enable anti-gravity", "Movement", {},
         "AntiGravity",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Run and dash in mid air", "Movement", {},
         "RunInMidAir",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Mid air d-pad version", "Movement", {},
         "MidAirDpad",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Mid air R stick version", "Movement", {},
         "MidAirStick",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Enhanced panic jump", "Movement", {},
         "EnhancedPanicJump",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
        {"Speed burst always", "Movement", {},
         "SpeedBurstAlways",
         "The published code patches a game function or needs a value the player picks; the built-in"
         "port is the implementation here."},
    };

    return codes;
}

} // namespace Gecko

