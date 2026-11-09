// The Mod Manager (mod_manager.h).
//
// A mod is a directory with a mod.ini; see the header for the format. The disc
// is never written: every mapped file is read into memory and handed to the
// disc image as an override, which serves it for this launch only.
#include "mod_manager.h"

#include "disc_image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace Mods {
namespace {

namespace fs = std::filesystem;

std::vector<Mod> g_mods;
std::vector<std::string> g_errors;
size_t g_applied = 0;
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

std::string StateFile() {
    return UserDirectory() + "/state.txt";
}

// The mod.ini parser: [sections], `key = value`, ';' and '#' comments. Small on
// purpose: the format has to stay something a mod's author can write by hand.
struct Ini {
    std::map<std::string, std::map<std::string, std::string>> sections;

    const std::map<std::string, std::string>* Section(std::string_view name) const {
        const auto found = sections.find(Lower(name));
        return found == sections.end() ? nullptr : &found->second;
    }

    std::string Get(std::string_view section, std::string_view key) const {
        const auto* found = Section(section);

        if (!found)
            return {};

        const auto value = found->find(Lower(key));
        return value == found->end() ? std::string{} : value->second;
    }
};

Ini ParseIni(std::ifstream& in) {
    Ini ini;
    std::string section;
    std::string line;

    while (std::getline(in, line)) {
        const size_t comment = line.find_first_of(";#");

        if (comment != std::string::npos)
            line = line.substr(0, comment);

        line = Trim(line);

        if (line.empty())
            continue;

        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
            continue;
        }

        const size_t equals = line.find('=');

        if (equals == std::string::npos)
            continue;

        ini.sections[section][Lower(Trim(std::string_view(line).substr(0, equals)))] =
            Trim(std::string_view(line).substr(equals + 1));
    }

    return ini;
}

void LoadState() {
    std::ifstream in(StateFile());
    std::string line;

    while (std::getline(in, line)) {
        const size_t tab = line.find('\t');

        if (tab == std::string::npos)
            continue;

        const bool on = line.substr(0, tab) == "1";
        const std::string id = line.substr(tab + 1);

        for (Mod& mod : g_mods) {
            if (mod.id != id)
                continue;

            mod.enabled = on && mod.problem.empty();
            break;
        }
    }
}

void SaveState() {
    std::ofstream out(StateFile(), std::ios::trunc);

    if (!out)
        return;

    out << "# MH3 mods: 0|1 <TAB> directory name\n";

    for (const Mod& mod : g_mods)
        out << (mod.enabled ? "1" : "0") << '\t' << mod.id << '\n';
}

// One mod's directory head: its mod.ini and the disc file it maps.
bool ReadMod(const fs::path& directory, Mod& mod) {
    mod.id = directory.filename().string();
    mod.root = directory.string();
    mod.name = mod.id;

    std::error_code error;
    const fs::path iniPath = directory / "mod.ini";

    if (!fs::is_regular_file(iniPath, error)) {
        mod.problem = "no mod.ini";
        return false;
    }

    std::ifstream in(iniPath);

    if (!in) {
        mod.problem = "mod.ini could not be read";
        return false;
    }

    const Ini ini = ParseIni(in);

    if (const std::string name = ini.Get("mod", "name"); !name.empty())
        mod.name = name;

    mod.author = ini.Get("mod", "author");
    mod.version = ini.Get("mod", "version");
    mod.game = ini.Get("mod", "game");

    if (const std::string order = ini.Get("mod", "order"); !order.empty())
        mod.order = std::atoi(order.c_str());

    // The disc id, when the mod names one: a mod for another region would be
    // applied to files that mean something else, or not at all.
    if (!mod.game.empty()) {
        const std::string target = "RMHP08";

        if (Lower(mod.game) != Lower(target))
            mod.problem = "for another disc (" + mod.game + ", this one is " + target + ")";
    }

    if (const auto* files = ini.Section("files")) {
        for (const auto& [discPath, modPath] : *files) {
            if (discPath.empty() || modPath.empty())
                continue;

            const fs::path source = directory / modPath;

            if (!fs::is_regular_file(source, error) || fs::file_size(source, error) == 0) {
                mod.problem = "missing " + modPath;
                continue;
            }

            mod.files.push_back({discPath, modPath});
        }
    }

    if (mod.files.empty() && mod.problem.empty())
        mod.problem = "nothing to install (no [files] entries)";

    return mod.problem.empty();
}

// Reads a mod's files into the disc image. A file whose length differs from
// the disc's is refused: the FST stores the length, so a shorter or longer
// replacement would desynchronise every later read.
size_t Install(const Mod& mod, std::string& error) {
    DiscImage& disc = GameDiscImage();
    size_t installed = 0;

    for (const FileMapping& mapping : mod.files) {
        const uint32_t discSize = disc.FileSizeOrZero(mapping.discPath);

        if (discSize == 0) {
            error = mapping.discPath + " is not on the disc";
            return installed;
        }

        std::error_code fsError;
        const fs::path source = fs::path(mod.root) / mapping.modPath;

        if (!fs::is_regular_file(source, fsError)) {
            error = "missing " + mapping.modPath;
            return installed;
        }

        const uintmax_t size = fs::file_size(source, fsError);

        if (size != discSize) {
            char message[160];
            std::snprintf(message, sizeof(message), "%s is %ju bytes, the disc's is %u (a mod must keep the length)",
                          mapping.discPath.c_str(), size, discSize);
            error = message;
            return installed;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream in(source, std::ios::binary);

        if (!in || !in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) {
            error = "could not read " + mapping.modPath;
            return installed;
        }

        try {
            disc.SetFileOverride(mapping.discPath, std::move(bytes));
            ++installed;
        } catch (const std::exception& failure) {
            error = failure.what();
            return installed;
        }
    }

    return installed;
}

void Scan() {
    g_mods.clear();
    g_errors.clear();
    g_applied = 0;

    const fs::path roots[] = {fs::path(UserDirectory())};

    for (const fs::path& root : roots) {
        std::error_code error;

        if (!fs::is_directory(root, error))
            continue;

        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (error || !entry.is_directory())
                continue;

            Mod mod;
            ReadMod(entry.path(), mod);
            g_mods.push_back(std::move(mod));
        }
    }

    std::sort(g_mods.begin(), g_mods.end(), [](const Mod& a, const Mod& b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });

    LoadState();
}

} // namespace

std::string UserDirectory() {
    const char* root = std::getenv("MH3_USER_DIR");
    std::string directory = root && *root ? std::string(root) : std::string("/storage/emulated/0/MH3");
    directory += "/mods";

    std::error_code error;
    fs::create_directories(directory, error);
    return directory;
}

void Initialize() {
    Scan();
    g_loaded = true;
}

const std::vector<Mod>& List() {
    if (!g_loaded)
        Initialize();

    return g_mods;
}

bool SetEnabled(size_t index, bool enabled) {
    if (!g_loaded)
        Initialize();

    if (index >= g_mods.size())
        return false;

    Mod& mod = g_mods[index];

    if (enabled && !mod.problem.empty())
        return false;

    mod.enabled = enabled;
    SaveState();
    return true;
}

void Save() {
    if (!g_loaded)
        Initialize();

    SaveState();
}

void Apply() {
    if (!g_loaded)
        Initialize();

    g_errors.clear();
    g_applied = 0;

    for (Mod& mod : g_mods) {
        mod.appliedFiles = 0;

        if (!mod.enabled)
            continue;

        if (!mod.problem.empty()) {
            g_errors.push_back(mod.name + ": " + mod.problem);
            mod.enabled = false;
            continue;
        }

        std::string error;
        mod.appliedFiles = Install(mod, error);

        if (!error.empty()) {
            g_errors.push_back(mod.name + ": " + error);
            mod.problem = error;
            mod.enabled = false;
            continue;
        }

        ++g_applied;
    }

    SaveState();
}

size_t AppliedCount() {
    return g_applied;
}

const std::vector<std::string>& Errors() {
    return g_errors;
}

void Rescan() {
    Scan();
}

} // namespace Mods
