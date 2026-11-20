#include "gecko/gecko_storage.h"

#include "gecko/gecko_compat.h"
#include "gecko/gecko_parser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Gecko {
namespace {

constexpr size_t kMaxFileBytes = 4u * 1024u * 1024u;

bool SanitizeName(const std::string& name, std::string& out) {
    out.clear();

    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')
            out += c;
    }

    if (out.empty())
        return false;

    if (out.size() > 64)
        out = out.substr(0, 64);

    return true;
}

std::string BuildCodesText(const std::vector<Code>& codes) {
    std::ostringstream text;
    bool first = true;

    for (const Code& code : codes) {
        if (!first)
            text << "\n";
        first = false;

        if (!code.sourceText.empty()) {
            text << code.sourceText;
        } else {
            text << "$" << code.name << "\n";
            char line[32];
            for (const Instruction& instruction : code.instructions) {
                std::snprintf(line, sizeof(line), "%08X %08X\n", instruction.addressWord, instruction.valueWord);
                text << line;
            }
        }

        text << "\n";
    }

    return text.str();
}

std::string BuildMetadata(const std::vector<Code>& codes) {
    std::ostringstream meta;

    for (const Code& code : codes) {
        meta << std::hex << code.id << ' '
             << (code.gameId.empty() ? "-" : code.gameId) << ' '
             << (code.region.empty() ? "-" : code.region) << '\n';
    }

    return meta.str();
}

bool AtomicWrite(const std::string& path, const std::string& data) {
    const auto directory = std::filesystem::path(path).parent_path();

    if (!directory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
    }

    const std::string tmpPath = path + ".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);

    if (!out)
        return false;

    out << data;
    out.close();

    if (!out)
        return false;

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);

    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmpPath, path, ec);
        if (ec)
            return false;
    }

    return true;
}

} // namespace

std::string UserDir() {
    const char* user = std::getenv("MH3_USER_DIR");

    if (user && *user)
        return user;

    return ".";
}

std::string GeckoDir() {
    return UserDir() + "/gecko";
}

std::string CodesPath() {
    return GeckoDir() + "/codes.txt";
}

std::string ImportsDir() {
    return GeckoDir() + "/imports";
}

bool LoadCodes(const std::string& path, std::vector<Code>& codes, std::vector<Diagnostic>& diagnostics) {
    std::error_code sizeEc;
    const uintmax_t size = std::filesystem::file_size(path, sizeEc);

    if (sizeEc)
        return true; // missing or unreadable storage is not an error

    if (size > kMaxFileBytes) {
        diagnostics.push_back({DiagnosticSeverity::Error, 0, {}, "saved code file is too large"});
        return false;
    }

    std::ifstream in(path, std::ios::binary);

    if (!in)
        return true; // missing storage is not an error

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    in.close();

    if (text.empty())
        return true;

    const ParseResult result = Parse(text);

    if (result.diagnostics.empty() && result.codes.empty())
        return true;

    // Preserve the original file if it had any problem at all.
    if (!result.diagnostics.empty()) {
        std::error_code ec;
        std::filesystem::copy_file(path, path + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::unordered_set<uint64_t> seen;

    for (const Code& code : result.codes) {
        if (seen.count(code.id) != 0)
            continue;

        seen.insert(code.id);
        codes.push_back(code);
    }

    diagnostics = result.diagnostics;

    for (const Code& code : result.codes)
        for (const Diagnostic& diagnostic : code.diagnostics)
            diagnostics.push_back(diagnostic);

    // Optional sidecar: per-code game ID/region, because the Gecko text
    // format only carries a file-level game ID.
    std::ifstream meta(path + ".meta");

    if (meta) {
        std::string line;

        while (std::getline(meta, line)) {
            std::istringstream row(line);
            std::string idText, gameId, region;

            if (!(row >> idText >> gameId >> region))
                continue;

            uint64_t id = 0;
            const auto [end, error] = std::from_chars(idText.data(), idText.data() + idText.size(), id, 16);

            if (error != std::errc() || end != idText.data() + idText.size())
                continue;

            for (Code& code : codes) {
                if (code.id != id)
                    continue;

                if (gameId != "-")
                    code.gameId = gameId;
                if (region != "-")
                    code.region = region;
                else if (!code.gameId.empty())
                    code.region = RegionFromGameId(code.gameId);

                code.gameCompatibility = ClassifyGameCompatibility(code.gameId, "RMHP08");
                break;
            }
        }
    }

    // Nothing parsed but the file had content: quarantine it.
    if (codes.empty() && !diagnostics.empty()) {
        std::error_code ec;
        std::filesystem::rename(path, path + ".corrupt", ec);
        diagnostics.push_back({DiagnosticSeverity::Error, 0, {}, "saved code file was quarantined as corrupt"});
        codes.clear();
    }

    return true;
}

bool SaveCodes(const std::string& path, const std::vector<Code>& codes) {
    const bool codesOk = AtomicWrite(path, BuildCodesText(codes));
    const bool metaOk = AtomicWrite(path + ".meta", BuildMetadata(codes));
    return codesOk && metaOk;
}

bool SaveImportedFile(const std::string& name, const std::string& text) {
    std::string safe;

    if (!SanitizeName(name, safe))
        safe = "import";

    std::string lower = safe;
    for (char& c : lower)
        c = char(std::tolower(static_cast<unsigned char>(c)));

    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".txt")
        safe += ".txt";

    return AtomicWrite(ImportsDir() + "/" + safe, text);
}

} // namespace Gecko
