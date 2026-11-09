// Host test for the cheat catalog (cheat_catalog.h).
//
// The catalog's whole job is agreement with the port's cheat table, and a table
// that drifts is worse than no table: a cheat with no row silently falls back to
// the keyword rule, and a row naming a cheat that was removed is dead weight the
// manager would show forever. So the checks are coverage, in both directions.
//
// LauncherCheats::List() lives in the runtime and pulls in Aurora, but the
// bundled Gecko library (gecko_db.cpp) is built from the same table - one entry
// per cheat, with the cheat's own name - so it stands in for it here, on a host,
// with no guest memory.
//
//   ./MH3CheatCatalogTests            run them all
#include "cheat_catalog.h"
#include "gecko_db.h"

#include <algorithm>
#include <cstdio>
#include <iterator> // std::size
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;

    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------------------

// Every cheat the port ships, as the bundled library lists it.
std::vector<std::string> CheatNames() {
    std::vector<std::string> names;

    for (const Gecko::DbCode& code : Gecko::DbCodes())
        names.emplace_back(code.name);

    return names;
}

void TestCoverage() {
    std::printf("coverage\n");

    const std::vector<std::string> names = CheatNames();
    Check(!names.empty(), "the library lists the port's cheats");

    // One row per cheat, no gaps: a gap would show as a section the manager
    // derived from a keyword guess rather than from this table.
    size_t missing = 0;
    for (const std::string& name : names) {
        if (!CheatCatalog::HasRow(name)) {
            std::printf("  no row for \"%s\"\n", name.c_str());
            ++missing;
        }
    }
    Check(missing == 0, "every cheat has a catalog row");

    // No stale row: every row names a cheat that still exists.
    Check(CheatCatalog::RowCount() == names.size(),
          "the table has exactly one row per cheat (" + std::to_string(CheatCatalog::RowCount()) +
              " rows, " + std::to_string(names.size()) + " cheats)");

    // The same, the other way round, with a set so a duplicated name shows up.
    std::set<std::string> set(names.begin(), names.end());
    Check(set.size() == names.size(), "the cheat names are unique");
}

void TestCategorisation() {
    std::printf("categorisation\n");

    const std::vector<std::string> names = CheatNames();

    size_t counts[size_t(CheatCatalog::Category::Count)]{};
    for (const std::string& name : names)
        ++counts[size_t(CheatCatalog::Of(name))];

    // Every section the manager shows has something in it: an empty section is a
    // header with nothing under it.
    for (size_t i = 0; i < size_t(CheatCatalog::Category::Count); ++i) {
        const auto category = CheatCatalog::Category(i);
        Check(counts[i] > 0, std::string("the ") + CheatCatalog::Name(category) + " section has cheats");
    }

    // Every category names itself, and the names are the manager's headers.
    for (size_t i = 0; i < size_t(CheatCatalog::Category::Count); ++i) {
        const char* name = CheatCatalog::Name(CheatCatalog::Category(i));
        Check(name && *name, "every section has a name");
    }

    // Spot checks of the table's own judgement: these are the ones a player would
    // notice being wrong.
    struct Case {
        const char* cheat;
        CheatCatalog::Category category;
    };
    constexpr Case kCases[]{
        {"Infinite health", CheatCatalog::Category::Player},
        {"No damage", CheatCatalog::Category::Player},
        {"Never poison", CheatCatalog::Category::Player},
        {"Infinite items", CheatCatalog::Category::Items},
        {"All items in the item box", CheatCatalog::Category::Items},
        {"1 hit kills", CheatCatalog::Category::Combat},
        {"Enemy has 1 HP (good for capture/cutting parts off)", CheatCatalog::Category::Combat},
        {"Infinite money", CheatCatalog::Category::Progression},
        {"All quests completed", CheatCatalog::Category::Progression},
        {"Show all monsters on the map", CheatCatalog::Category::World},
        {"Free fly", CheatCatalog::Category::Movement},
        {"Moon jump", CheatCatalog::Category::Movement},
        {"Walk through walls (on/off)", CheatCatalog::Category::Movement},
        {"Language changer", CheatCatalog::Category::Misc},
        {"Hair dye (red)", CheatCatalog::Category::Misc},
        {"Join/host any quest", CheatCatalog::Category::Online},
    };

    for (const Case& test : kCases) {
        const auto got = CheatCatalog::Of(test.cheat);
        Check(got == test.category,
              std::string("\"") + test.cheat + "\" is " + CheatCatalog::Name(test.category) +
                  " (got " + CheatCatalog::Name(got) + ")");
    }
}

void TestKeywords() {
    std::printf("keywords\n");

    // A cheat the table has never seen still lands somewhere sensible: the
    // fallback is what keeps a later addition out of Misc.
    Check(!CheatCatalog::HasRow("Infinite barrel bombs of doom"), "the fallback case is not a row");
    Check(CheatCatalog::Of("Infinite barrel bombs of doom") == CheatCatalog::Category::Items,
          "a new item cheat falls into Items");

    const auto unknown = CheatCatalog::Of("Some entirely unprecedented thing");
    Check(unknown == CheatCatalog::Category::Misc, "an unrecognisable name falls into Misc");
    Check(CheatCatalog::Name(unknown) != nullptr, "and still names its section");

    // Lower case and no dependence on the row's capitalisation.
    Check(CheatCatalog::Of("infinite HEALTH") == CheatCatalog::Category::Player,
          "the keyword rule ignores case");

    // A section's index is the category's value, in the manager's order.
    Check(CheatCatalog::SectionOf("Moon jump") == size_t(CheatCatalog::Category::Movement),
          "a section index is its category's position");
    Check(CheatCatalog::SectionNames().size() == size_t(CheatCatalog::Category::Count),
          "every category has a section name");
}

} // namespace

int main() {
    TestCoverage();
    TestCategorisation();
    TestKeywords();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
