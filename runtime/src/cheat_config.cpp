// Saving the player's cheat choices (cheat_config.h).
//
// The file is plain text so it can be read, edited and shared; every row also
// carries the cheat's name, which is what makes a saved file survive a change
// to the table (a new cheat in the middle moves every index after it).
#include "cheat_config.h"

#include "launcher_cheats.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace CheatConfig {
namespace {

namespace fs = std::filesystem;

constexpr const char* kMagic = "MH3CHEATS 1";

std::string Escape(const std::string& text) {
    // The text of a cheat is printable ASCII with no spaces at the ends; a
    // quote would still break the row, so it is doubled.
    std::string out;

    for (char c : text) {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }

    return out;
}

std::vector<std::string> SplitRow(const std::string& row) {
    // CHEAT "name" on choice "text": the name is quoted, so splitting on
    // spaces outside quotes is enough.
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;

    for (size_t i = 0; i < row.size(); ++i) {
        const char c = row[i];

        if (c == '"') {
            if (quoted && i + 1 < row.size() && row[i + 1] == '"') {
                current += '"';
                ++i;
                continue;
            }

            quoted = !quoted;
            continue;
        }

        if (!quoted && (c == ' ' || c == '\t')) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }

            continue;
        }

        current += c;
    }

    if (!current.empty())
        parts.push_back(current);
    return parts;
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

std::string FilePath() {
    return UserDirectory() + "/native.txt";
}

void Load() {
    std::ifstream in(FilePath());

    if (!in)
        return;

    std::string line;
    bool header = false;

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        if (!header) {
            if (line.rfind(kMagic, 0) != 0)
                return; // some other file: leave the cheats alone

            header = true;
            continue;
        }

        const std::vector<std::string> parts = SplitRow(line);

        if (parts.size() < 4 || parts[0] != "CHEAT")
            continue;

        const std::string& name = parts[1];

        // The index is the fast path; the name is the truth.
        const auto list = LauncherCheats::List();
        size_t index = list.size();

        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].name && name == list[i].name) {
                index = i;
                break;
            }

        if (index == list.size())
            continue;

        const bool on = parts[2] == "1";
        const size_t choice = size_t(std::strtoul(parts[3].c_str(), nullptr, 10));

        if (choice < LauncherCheats::Choices(index))
            LauncherCheats::Select(index, choice);

        if (parts.size() > 4 && LauncherCheats::TextLength(index) != 0)
            LauncherCheats::SetText(index, parts[4]);

        LauncherCheats::Enable(index, on);
    }
}

void Save() {
    std::ofstream out(FilePath(), std::ios::trunc);

    if (!out)
        return;

    out << kMagic << '\n';
    out << "# CHEAT \"name\" on choice \"typed text\". The name decides which "
           "cheat a row is for, so this file survives updates to the list.\n";

    const auto list = LauncherCheats::List();

    for (size_t i = 0; i < list.size(); ++i) {
        const auto& cheat = list[i];

        out << "CHEAT \"" << Escape(cheat.name ? cheat.name : "") << "\" "
            << (LauncherCheats::Enabled(i) ? 1 : 0) << ' '
            << LauncherCheats::Selected(i) << " \"" << Escape(LauncherCheats::Text(i)) << "\"\n";
    }
}

void SaveOne(size_t index) {
    // Rewriting the whole file keeps it consistent; it is a few hundred short
    // lines, once per tap.
    Save();
}

} // namespace CheatConfig
