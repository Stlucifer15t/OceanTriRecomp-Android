// The Gecko manager: the bundled library, the user's codes, their enabled
// states, and the per-step pass (gecko.h).
//
// Files, under $MH3_USER_DIR/cheats:
//   gecko.txt         the user's codes, in the format the community uses
//                     ($Name headers, 8+8 hex lines, an "# enabled: 0|1" line)
//   state.txt         which codes are on, bundled ones included, by name
// Nothing here touches the disc or the app's assets.
#include "gecko.h"

#include "cheats.h"
#include "gecko_db.h"
#include "gecko_engine.h"
#include "memory.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Gecko {
namespace {

namespace fs = std::filesystem;

Region g_target = Region::Pal;
std::vector<Entry> g_entries;
std::vector<std::string> g_lastErrors;
bool g_loaded = false;

std::string Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);

    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);

    return std::string(text);
}

std::string Lower(std::string_view text) {
    std::string out(text);

    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));

    return out;
}

std::string CodesFile() {
    return UserDirectory() + "/gecko.txt";
}

std::string StateFile() {
    return UserDirectory() + "/state.txt";
}

// The state file is a list of "0|1<TAB>name" rows: names, not indices, so a
// library change never moves a switch onto a different cheat.
void LoadState() {
    std::ifstream in(StateFile());
    std::string line;

    while (std::getline(in, line)) {
        const size_t tab = line.find('\t');

        if (tab == std::string::npos)
            continue;

        const bool on = line.substr(0, tab) == "1";
        const std::string name = line.substr(tab + 1);

        for (Entry& entry : g_entries) {
            if (entry.code.name != name)
                continue;

            // A code that cannot run stays off, whatever the file says.
            entry.enabled = on && entry.blocked.empty();
            break;
        }
    }
}

void SaveState() {
    std::ofstream out(StateFile(), std::ios::trunc);

    if (!out)
        return;

    out << "# MH3 Gecko codes: 0|1 <TAB> name. A code this disc cannot run "
           "stays off.\n";

    for (const Entry& entry : g_entries)
        out << (entry.enabled ? "1" : "0") << '\t' << entry.code.name << '\n';
}

void SaveUserCodes() {
    std::ofstream out(CodesFile(), std::ios::trunc);

    if (!out)
        return;

    std::vector<Code> custom;

    for (const Entry& entry : g_entries)
        if (entry.source != Source::Builtin)
            custom.push_back(entry.code);

    out << FormatAll(custom, g_target);
}

void LoadUserCodes() {
    std::ifstream in(CodesFile());

    if (!in)
        return;

    std::stringstream buffer;
    buffer << in.rdbuf();

    const ParsedList parsed = Parse(buffer.str());

    for (const Code& code : parsed.codes) {
        Entry entry;
        entry.code = code;
        entry.source = Source::Imported;
        entry.blocked = UnsupportedReason(code);
        entry.plan = Plan(code, g_target);
        g_entries.push_back(std::move(entry));
    }
}

void AddBuiltin() {
    for (const DbCode& db : DbCodes()) {
        Entry entry;
        entry.code.name = db.name;
        entry.code.body = db.body;
        entry.code.sourceRegion = Region::Pal; // the addresses are this disc's
        entry.code.note = db.note;
        entry.source = Source::Builtin;
        entry.group = db.group ? db.group : "";
        entry.native = db.native ? db.native : "";
        entry.blocked = UnsupportedReason(entry.code);
        entry.plan = Plan(entry.code, g_target);
        g_entries.push_back(std::move(entry));
    }
}

// The managed list never shares names: two entries with one name would share
// a switch in state.txt.
void DropDuplicateNames() {
    for (size_t i = 0; i < g_entries.size(); ++i)
        for (size_t j = g_entries.size(); j-- > i + 1;) {
            if (Lower(g_entries[j].code.name) == Lower(g_entries[i].code.name))
                g_entries.erase(g_entries.begin() + std::ptrdiff_t(j));
        }
}

void EnsureLoaded() {
    if (!g_loaded)
        Initialize();
}

} // namespace

std::string UserDirectory() {
    const char* root = std::getenv("MH3_USER_DIR");
    std::string directory = root && *root ? std::string(root) : std::string("/storage/emulated/0/MH3");
    directory += "/cheats";

    std::error_code error;
    fs::create_directories(directory, error);
    return directory;
}

void Initialize() {
    g_entries.clear();
    g_lastErrors.clear();

    AddBuiltin();
    LoadUserCodes();
    DropDuplicateNames();

    g_lastErrors.resize(g_entries.size());
    LoadState();

    g_loaded = true;
}

const std::vector<Entry>& Entries() {
    EnsureLoaded();
    return g_entries;
}

size_t AddCodes(const std::vector<Code>& codes, Source source) {
    EnsureLoaded();

    size_t added = 0;

    for (const Code& code : codes) {
        Entry entry;
        entry.code = code;
        entry.source = source;
        entry.blocked = UnsupportedReason(code);
        entry.plan = Plan(code, g_target);
        g_entries.push_back(std::move(entry));
        g_lastErrors.emplace_back();
        ++added;
    }

    if (added) {
        SaveUserCodes();
        SaveState();
    }

    return added;
}

size_t ImportText(std::string_view text, Source source, std::string& error) {
    const ParsedList parsed = Parse(text);

    if (!parsed.errors.empty()) {
        const Error& first = parsed.errors.front();
        error = "line " + std::to_string(first.line) + ": " + first.message;
    }

    return AddCodes(parsed.codes, source);
}

bool SetEnabled(size_t index, bool enabled) {
    EnsureLoaded();

    if (index >= g_entries.size())
        return false;

    Entry& entry = g_entries[index];

    if (enabled && !entry.blocked.empty())
        return false;

    entry.enabled = enabled;
    SaveState();
    return true;
}

void Remove(size_t index) {
    EnsureLoaded();

    if (index >= g_entries.size() || g_entries[index].source == Source::Builtin)
        return;

    g_entries.erase(g_entries.begin() + std::ptrdiff_t(index));

    if (index < g_lastErrors.size())
        g_lastErrors.erase(g_lastErrors.begin() + std::ptrdiff_t(index));

    SaveUserCodes();
    SaveState();
}

void ApplyRegion(Region source) {
    EnsureLoaded();

    g_target = source;

    for (Entry& entry : g_entries) {
        entry.plan = Plan(entry.code, source);
        entry.blocked = UnsupportedReason(entry.code);

        // An address that cannot be translated leaves the code unrunnable
        // rather than writing to the wrong place.
        for (size_t i = 0; i < entry.code.body.size() && entry.blocked.empty(); ++i) {
            const Region from = entry.code.sourceRegion;

            if (from == Region::Unknown || from == source)
                continue;

            const Translation translated = TranslateRegion(entry.code.body[i].address & 0x01FFFFFEu, from, source);

            if (translated.error != nullptr)
                entry.blocked = std::string(translated.error) + " (line " + std::to_string(i + 1) + ")";
        }

        if (!entry.blocked.empty())
            entry.enabled = false;
    }
}

Region TargetRegion() {
    return g_target;
}

void Save() {
    EnsureLoaded();
    SaveUserCodes();
    SaveState();
}

void Step() {
    EnsureLoaded();

    bool any = false;

    for (const Entry& entry : g_entries)
        if (entry.enabled) {
            any = true;
            break;
        }

    if (!any)
        return;

    const uint16_t held = Cheats::Held();

    for (size_t index = 0; index < g_entries.size(); ++index) {
        Entry& entry = g_entries[index];

        if (!entry.enabled || !entry.blocked.empty())
            continue;

        // The plan is what runs: it is the code body with this disc's
        // addresses. An activator comes along with it.
        Code executable;
        executable.name = entry.code.name;
        executable.sourceRegion = g_target;
        executable.activator = entry.code.activator;
        executable.body = entry.plan;

        Options options;
        options.held = held;

        const Result result = Run(executable, GuestMemory(), options);

        if (!result.ok) {
            // A code that fails (a pointer is null in a menu, say) is reported
            // once and switched off: retrying it every step would spam the log.
            g_lastErrors[index] = result.error;
            entry.enabled = false;
        } else {
            g_lastErrors[index].clear();
        }
    }
}

bool RunOnce(size_t index, std::string& error) {
    EnsureLoaded();

    if (index >= g_entries.size()) {
        error = "no such code";
        return false;
    }

    const Entry& entry = g_entries[index];

    if (!entry.blocked.empty()) {
        error = entry.blocked;
        return false;
    }

    Code executable;
    executable.name = entry.code.name;
    executable.sourceRegion = g_target;
    executable.activator = entry.code.activator;
    executable.body = entry.plan;

    const Result result = RunOnce(executable, GuestMemory());

    if (!result.ok) {
        error = result.error;
        return false;
    }

    return true;
}

std::string LastError(size_t index) {
    if (index >= g_lastErrors.size())
        return {};

    return g_lastErrors[index];
}

size_t Count() {
    EnsureLoaded();
    return g_entries.size();
}

size_t EnabledCount() {
    EnsureLoaded();

    return size_t(std::count_if(g_entries.begin(), g_entries.end(), [](const Entry& entry) {
        return entry.enabled;
    }));
}

size_t RunnableCount() {
    EnsureLoaded();

    return size_t(std::count_if(g_entries.begin(), g_entries.end(), [](const Entry& entry) {
        return entry.blocked.empty();
    }));
}

size_t NativeBackedCount() {
    EnsureLoaded();

    return size_t(std::count_if(g_entries.begin(), g_entries.end(), [](const Entry& entry) {
        return entry.code.body.empty();
    }));
}

std::vector<std::string> Groups() {
    std::vector<std::string> groups;

    EnsureLoaded();

    for (const Entry& entry : g_entries) {
        const std::string& group = entry.group;

        if (std::find(groups.begin(), groups.end(), group) == groups.end())
            groups.push_back(group);
    }

    return groups;
}

} // namespace Gecko
