// The category of every native cheat (cheat_catalog.h).
//
// Keyed by the cheat's name, not its index: the port's table groups by who wrote
// the original code, and inserting a cheat there must not move every other
// cheat's section. A test (runtime/src/cheat_tests.cpp) checks that every entry
// of LauncherCheats::List() has a row here and that no row names a cheat that no
// longer exists, so the table cannot rot.
//
// The order below is the manager's section order; within a section the rows are
// the port's own order, so a cheat keeps its neighbours.
#include "cheat_catalog.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace CheatCatalog {
namespace {

struct Row {
    const char* name;
    Category category;
};

constexpr Row kRows[]{

    // Player
    {"No damage", Category::Player},
    {"No action stamina drain", Category::Player},
    {"Felvine underwater", Category::Player},
    {"Smoke underwater", Category::Player},
    {"Steak underwater", Category::Player},
    {"Alternate infinite health", Category::Player},
    {"Health never changes", Category::Player},
    {"Daredevil mode", Category::Player},
    {"Never stunned", Category::Player},
    {"Infinite health", Category::Player},
    {"Infinite stamina", Category::Player},
    {"Inf air", Category::Player},
    {"Max fire", Category::Player},
    {"Max water", Category::Player},
    {"Max thunder", Category::Player},
    {"Max dragon", Category::Player},
    {"Max ice", Category::Player},
    {"Never life drain", Category::Player},
    {"Add 5 to dragon", Category::Player},
    {"Add 5 to thunder", Category::Player},
    {"Add 5 to ice", Category::Player},
    {"Add 5 to fire (effect)", Category::Player},
    {"Adds 5 to water", Category::Player},
    {"No negative ice stats effect", Category::Player},
    {"No negative dragon stats effect", Category::Player},
    {"No negative water stats effect", Category::Player},
    {"No negative thunder stats effect", Category::Player},
    {"No negative fire stats", Category::Player},
    {"Never get defense down", Category::Player},
    {"Never get felvine-scented", Category::Player},
    {"Never get snowman", Category::Player},
    {"Never get muddy", Category::Player},
    {"Always windproof", Category::Player},
    {"Never poison", Category::Player},
    {"Always have natural rec up", Category::Player},
    {"Health bar flash purple", Category::Player},
    {"No element", Category::Player},
    {"Felyne skill 1 button activation", Category::Player},
    {"Felyne skill 2 button activation", Category::Player},
    {"Felyne skill 3 button activation", Category::Player},
    {"Felyne skill 4 button activation", Category::Player},
    {"Unlimited cold drink", Category::Player},
    {"Unlimited hot drink", Category::Player},
    {"Atk up durations have no limit", Category::Player},
    {"Def up durations have no limit", Category::Player},

    // Items
    {"All items in the item box", Category::Items},
    {"All equipment in the equipment box", Category::Items},
    {"Equip blade/gunner armor", Category::Items},
    {"Enable armor pigment for all armor", Category::Items},
    {"Can't carve/gather (on/off)", Category::Items},
    {"Talismans sent to box start at slot", Category::Items},
    {"Infinite bombs in pouch", Category::Items},
    {"Set unlimited traps", Category::Items},
    {"Set unlimited barrel bombs", Category::Items},
    {"Infinite items", Category::Items},
    {"All boxes have 8 pages", Category::Items},
    {"Item pouch items x10", Category::Items},
    {"All item box items x99", Category::Items},
    {"Infinite ressources", Category::Items},
    {"Inf gathering", Category::Items},
    {"Inf carving", Category::Items},
    {"Gathering/carving mod", Category::Items},
    {"Item/equip boxes have 8 pages", Category::Items},
    {"Res 0", Category::Items},
    {"Gain/use items/ammo get 10", Category::Items},
    {"Load XX shots", Category::Items},
    {"Inf ammo", Category::Items},
    {"Enable all ammo v2", Category::Items},
    {"Instant well-done steak", Category::Items},
    {"Force grill", Category::Items},
    {"Equipped helm level 255", Category::Items},
    {"Equipped vest level 255", Category::Items},
    {"Equipped gloves level 255 (quest only)", Category::Items},
    {"Equipped belt level 255 (quest only)", Category::Items},
    {"Equipped pants level 255 (quest only)", Category::Items},
    {"Activate armor skills for all helmet", Category::Items},
    {"Activate armor skills for all chest", Category::Items},
    {"Activate armor skills for all arms", Category::Items},
    {"Activate armor skills for all waist", Category::Items},
    {"Activate armor skills for all legs", Category::Items},
    {"Deactivate armor skills codes (turn off)", Category::Items},

    // Combat
    {"Tranq attaq", Category::Combat},
    {"Cursed weapons", Category::Combat},
    {"No knockback", Category::Combat},
    {"Monster HP multipler", Category::Combat},
    {"Monsters have regen during rage", Category::Combat},
    {"Instant rage mode", Category::Combat},
    {"Infinite rage mode", Category::Combat},
    {"Infinite monster stamina", Category::Combat},
    {"Monster speed modifier", Category::Combat},
    {"Max heal all monsters", Category::Combat},
    {"Felvine lasts forever on monster", Category::Combat},
    {"Monster can't see you", Category::Combat},
    {"Action repeater for targeted monster", Category::Combat},
    {"Freeze/unfreeze monsters", Category::Combat},
    {"Weapons always sharp (best version)", Category::Combat},
    {"Speed combos never disabled", Category::Combat},
    {"Combine never disabled", Category::Combat},
    {"Long swords max sharpness", Category::Combat},
    {"Crossbow rapid fire", Category::Combat},
    {"No reload", Category::Combat},
    {"1 hit kills", Category::Combat},
    {"Enemy has 1 HP (good for capture/cutting parts off)", Category::Combat},
    {"Sharpness stays full", Category::Combat},
    {"On hit long sword power max (charge bar)", Category::Combat},
    {"Weapon max attack base (press buttons when on weapon)", Category::Combat},
    {"Max sharp (press buttons when on weapon)", Category::Combat},
    {"Max affinity (press buttons when on weapon)", Category::Combat},
    {"Boss/s mod", Category::Combat},
    {"Set boss/s size to", Category::Combat},
    {"Set boss/s level to", Category::Combat},
    {"Defense upgrade (255)", Category::Combat},
    {"Defense max (255)", Category::Combat},
    {"Attack upgrade (255)", Category::Combat},
    {"Add 1000 to attack", Category::Combat},
    {"Add to defense", Category::Combat},
    {"Faster weapon usage", Category::Combat},
    {"Enhanced focus (improved weapon charge for GS, HA, LS, SW)", Category::Combat},
    {"GreatSword no overcharging", Category::Combat},
    {"GreatSword inf charge and Lv3 charge always", Category::Combat},
    {"Sword and shield inf slashes", Category::Combat},
    {"Hammer max charge always", Category::Combat},
    {"Lance charge speed increased", Category::Combat},
    {"Lance charge speed controller", Category::Combat},
    {"Battojutsu mode (long sword quick draw)", Category::Combat},
    {"Force switch axe sword mode", Category::Combat},
    {"Rapidfire all ammo", Category::Combat},
    {"Instant sharpen", Category::Combat},
    {"Force sheathe weapon", Category::Combat},
    {"Force unsheathe weapon", Category::Combat},
    {"Exceed weapon size on attack", Category::Combat},
    {"Freeze monsters", Category::Combat},
    {"Switch axe power gauge always full", Category::Combat},
    {"Range for all bowgun barrels", Category::Combat},

    // Progression
    {"Armor is upgradeable to high rank even at low rank", Category::Progression},
    {"Don't save new arena records", Category::Progression},
    {"No HRP gain/loss", Category::Progression},
    {"Inf retries + reward increased instead", Category::Progression},
    {"Autofail", Category::Progression},
    {"Autoclear (main and subs)", Category::Progression},
    {"Defeats counter always 0", Category::Progression},
    {"Always display HR infos on status", Category::Progression},
    {"Purchase any weapon with HR0", Category::Progression},
    {"Hunter ranking modifier", Category::Progression},
    {"Infinite time for choosing your equipment", Category::Progression},
    {"No need to have required materials", Category::Progression},
    {"No playtime increase", Category::Progression},
    {"No need for money", Category::Progression},
    {"Unlock all star quests", Category::Progression},
    {"All quests completed", Category::Progression},
    {"Always able to sleep", Category::Progression},
    {"Max res on converting", Category::Progression},
    {"Sell/use item get 10", Category::Progression},
    {"Buy new item get 10", Category::Progression},
    {"Infinite money", Category::Progression},
    {"All items cost 1z in item stores", Category::Progression},
    {"Dont lose money buying items", Category::Progression},
    {"Smithy forge/upgrade with 1 item", Category::Progression},
    {"Set smithy req materials to", Category::Progression},
    {"Infinite time arena and quests", Category::Progression},
    {"Set playtime", Category::Progression},
    {"Enable all hunting fleets", Category::Progression},
    {"Hunting fleet captain always has catch", Category::Progression},
    {"Hunting fleet captain always 4 stars", Category::Progression},
    {"Hunting fleet Redspear always has catch", Category::Progression},
    {"Hunting fleet Redspear always 4 stars", Category::Progression},
    {"Hunting fleet Blackspear always has catch", Category::Progression},
    {"Hunting fleet Blackspear always 4 stars", Category::Progression},
    {"All hunting fleets: half a day voyages", Category::Progression},
    {"1 million zenny prize from Chamberlyne", Category::Progression},
    {"Enable all challenges in offline arena", Category::Progression},
    {"Enable all Felyne helpers", Category::Progression},
    {"Max out all farm stats", Category::Progression},
    {"Instant farming for all Felyne helpers", Category::Progression},
    {"Each monster kill at hunt report", Category::Progression},

    // World
    {"Show all monsters on the map", Category::World},
    {"Can't leave area (on/off)", Category::World},
    {"Farcaster at camp", Category::World},
    {"Pitfall at camp", Category::World},
    {"Shock at camp", Category::World},
    {"LBB at camp", Category::World},
    {"Farcaster in phase 2", Category::World},
    {"Hold to hide rewards", Category::World},
    {"Show real raw", Category::World},
    {"Show real element", Category::World},
    {"Show real status", Category::World},
    {"Map attached to HUD", Category::World},
    {"No quest box jiggle", Category::World},
    {"Change to morning (upon loading)", Category::World},
    {"Change to evening (upon loading)", Category::World},
    {"Cancel camera zoom-in when falling", Category::World},
    {"Boss 1 Moga forecast tomorrow", Category::World},
    {"Boss 2 Moga forecast tomorrow", Category::World},
    {"Boss 1 Moga forecast day after", Category::World},
    {"Boss 2 Moga forecast day after", Category::World},
    {"Sleep anywhere", Category::World},
    {"Thrown back to camp", Category::World},
    {"Slow-mo mode", Category::World},

    // Movement
    {"Speed hack in village", Category::Movement},
    {"Snail speed (village only)", Category::Movement},
    {"Super speed", Category::Movement},
    {"Action repeater", Category::Movement},
    {"Moon jump", Category::Movement},
    {"Teleporter", Category::Movement},
    {"Free fly", Category::Movement},
    {"Speed up when running", Category::Movement},
    {"Enhanced leap at will (press dash and action button)", Category::Movement},
    {"Jump at will (press the action button to jump)", Category::Movement},
    {"Jump very high (press dash and action button)", Category::Movement},
    {"Always face forward when using R stick", Category::Movement},
    {"Always face forward when using left/right d-pad", Category::Movement},
    {"Moonjump (A button)", Category::Movement},
    {"Glider (single play)", Category::Movement},
    {"Enable anti-gravity", Category::Movement},
    {"Run and dash in mid air", Category::Movement},
    {"Mid air d-pad version", Category::Movement},
    {"Mid air R stick version", Category::Movement},
    {"Air attack (great sword)", Category::Movement},
    {"Air attack (sword and shield)", Category::Movement},
    {"Air attack (hammer)", Category::Movement},
    {"Air attack (lance)", Category::Movement},
    {"Air attack (long sword)", Category::Movement},
    {"Air attack (switch axe)", Category::Movement},
    {"Air attack (bowgun)", Category::Movement},
    {"Force evade (quick recover)", Category::Movement},
    {"Enhanced movement speed (quest/village)", Category::Movement},
    {"Enhanced dashing (quest/village)", Category::Movement},
    {"Enhanced panic jump", Category::Movement},
    {"Enhanced evade distances", Category::Movement},
    {"Evade distance x2 (needs Enhanced evade distances)", Category::Movement},
    {"Evade distance x10 (needs Enhanced evade distances)", Category::Movement},
    {"Walk through walls (on/off)", Category::Movement},
    {"Pass through monsters (on/off)", Category::Movement},
    {"Speed burst always", Category::Movement},

    // Online
    {"Weapons ultra sharp (online code)", Category::Online},
    {"Speed glide/skate (online code)", Category::Online},
    {"Increase oxygen bar (online code)", Category::Online},
    {"Join/host any quest", Category::Online},

    // Misc
    {"Unlock all gallery clips", Category::Misc},
    {"Turn it up to 11", Category::Misc},
    {"Language changer", Category::Misc},
    {"Instant flex", Category::Misc},
    {"Can't save", Category::Misc},
    {"Character name change", Category::Misc},
    {"Ghost world", Category::Misc},
    {"Sex change", Category::Misc},
    {"Puddy face", Category::Misc},
    {"Hair dresser", Category::Misc},
    {"Hair dye (red)", Category::Misc},
    {"Hair dye (green)", Category::Misc},
    {"Hair dye (blue)", Category::Misc},
    {"Clothing change", Category::Misc},
    {"Voice change", Category::Misc},
    {"Eye color change", Category::Misc},
    {"Features", Category::Misc},
    {"Color changing clothing (red)", Category::Misc},
    {"Color changing clothing (green)", Category::Misc},
    {"Color changing clothing (blue)", Category::Misc},
    {"Combo list complete", Category::Misc},
    {"Quick gestures", Category::Misc},
    {"Skin tone change", Category::Misc},
};

constexpr const char* kNames[]{
    "Player", "Items", "Combat", "Progression", "World", "Movement", "Online", "Misc",
};

static_assert(std::size(kRows) > 0, "the catalog cannot be empty");

// Lower-case, for the keyword rule: the names are written for players, so case
// and punctuation vary.
std::string Lower(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return lower;
}

bool Has(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

} // namespace

const char* Name(Category category) {
    const size_t index = size_t(category);
    return index < kCategoryCount ? kNames[index] : kNames[size_t(Category::Misc)];
}

Category Of(std::string_view cheatName) {
    for (const Row& row : kRows)
        if (cheatName == row.name)
            return row.category;

    // A cheat this table does not know - one added to the port later - still
    // lands somewhere sensible: the first rule that matches its name, and Misc
    // if none does. The order is the order of the sections (a status effect is
    // a player matter even when the word "monster" appears).
    const std::string name = Lower(cheatName);

    if (Has(name, "quest") || Has(name, "money") || Has(name, "zenny") || Has(name, "hr") ||
        Has(name, "smithy") || Has(name, "farm") || Has(name, "arena") || Has(name, "fleet") ||
        Has(name, "gallery") || Has(name, "rank"))
        return Category::Progression;

    if (Has(name, "map") || Has(name, "hud") || Has(name, "weather") || Has(name, "clock") ||
        Has(name, "time") || Has(name, "farcaster") || Has(name, "forecast") || Has(name, "camp"))
        return Category::World;

    if (Has(name, "monster") || Has(name, "rage") || Has(name, "sharp") || Has(name, "attack") ||
        Has(name, "defen") || Has(name, "damage") || Has(name, "kill") || Has(name, "affinity") ||
        Has(name, "charge") || Has(name, "tranq") || Has(name, "ammo") || Has(name, "evade"))
        return Category::Combat;

    if (Has(name, "item") || Has(name, "box") || Has(name, "pouch") || Has(name, "bomb") ||
        Has(name, "trap") || Has(name, "carve") || Has(name, "gather") || Has(name, "drink") ||
        Has(name, "steak") || Has(name, "ressources") || Has(name, "equipment") ||
        Has(name, "armor") || Has(name, "helm"))
        return Category::Items;

    if (Has(name, "jump") || Has(name, "fly") || Has(name, "speed") || Has(name, "dash") ||
        Has(name, "move") || Has(name, "wall") || Has(name, "teleport") || Has(name, "glide") ||
        Has(name, "gravity") || Has(name, "air"))
        return Category::Movement;

    if (Has(name, "health") || Has(name, "stamina") || Has(name, "oxygen") || Has(name, "poison") ||
        Has(name, "element") || Has(name, "status") || Has(name, "felvine") || Has(name, "blight") ||
        Has(name, "windproof") || Has(name, "recovery"))
        return Category::Player;

    if (Has(name, "online") || Has(name, "host") || Has(name, "join"))
        return Category::Online;

    return Category::Misc;
}

bool HasRow(std::string_view cheatName) {
    return std::any_of(std::begin(kRows), std::end(kRows),
                       [cheatName](const Row& row) { return cheatName == row.name; });
}

size_t RowCount() { return std::size(kRows); }

size_t SectionOf(std::string_view cheatName) { return size_t(Of(cheatName)); }

std::span<const std::string_view> SectionNames() {
    static const std::string_view names[]{
        "Player", "Items", "Combat", "Progression", "World", "Movement", "Online", "Misc",
    };
    return names;
}

} // namespace CheatCatalog
