#include "custom_codes.h"

#include "cheats.h"
#include "fs_status.h"
#include "memory.h"
#include "net/local_server.h"
#include "net/online_session.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

namespace CustomCodes {
namespace {

struct Mapping {
    uint32_t source = 0, target = 0;
    uint8_t width = 0;
};

std::mutex g_mutex;
std::filesystem::path g_root;
std::vector<Code> g_codes;
std::vector<uint8_t> g_enabled;
std::vector<std::pair<uint64_t, bool>> g_pending;
std::string g_status = "No mapped custom codes imported.";
bool g_dirty = false;

std::string_view Trim(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
        line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);
    return line;
}

bool Hex(std::string_view text, uint32_t& value, size_t digits = 8) {
    if (text.size() != digits)
        return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return error == std::errc{} && end == text.data() + text.size();
}

std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> result;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        result.push_back(Trim(text.substr(at, end == text.npos ? text.npos : end - at)));
        if (end == text.npos)
            break;
        at = end + 1;
        if (result.size() > 2048)
            break;
    }
    return result;
}

bool KnownGame(std::string_view id) {
    return id == "RMHP08" || id == "RMHE08" || id == "RMHJ08";
}

bool DataTarget(uint32_t address, uint8_t width) {
    // MEM1's translated executable/DOL area is excluded. MEM2 is the game's
    // heap. The runtime still checks the exact allocation with Contains().
    return (address >= 0x80500000u && address <= 0x817FFFFFu - width) ||
           (address >= 0x90000000u && address <= 0x93FFFFFFu - width);
}

CheatManager::Result ParseMapping(std::string_view text, std::string& sourceGame, std::vector<Mapping>& mappings) {
    const auto lines = Lines(text);
    if (lines.size() > 2048)
        return {false, "Mapping file exceeds 2048 lines."};
    bool header = false, target = false, source = false;
    for (const auto line : lines) {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream row{std::string(line)};
        std::string tag, a, b, c;
        row >> tag;
        if (tag == "OCEANTRI-GECKO-MAP") {
            if (header || !(row >> a) || a != "1" || (row >> a))
                return {false, "Expected OCEANTRI-GECKO-MAP 1 header."};
            header = true;
        } else if (tag == "target" || tag == "source") {
            if (!header || !(row >> a) || (row >> b) || !KnownGame(a))
                return {false, "Invalid source/target game declaration."};
            if (tag == "target") {
                if (target || a != "RMHP08")
                    return {false, "Only the supported PAL target RMHP08 is admitted."};
                target = true;
            } else {
                if (source)
                    return {false, "Duplicate source declaration."};
                sourceGame = a;
                source = true;
            }
        } else if (tag == "map") {
            if (!header || !target || !source || !(row >> a >> b >> c) || (row >> tag))
                return {false, "Expected: map <source hex> <PAL target hex> <1|2|4>."};
            Mapping map;
            uint32_t width;
            if (!Hex(a, map.source) || !Hex(b, map.target) || !Hex(c, width, 1) || (width != 1 && width != 2 && width != 4))
                return {false, "Invalid exact address mapping."};
            map.width = uint8_t(width);
            if (map.source < 0x80000000u || map.source > 0x81FFFFFFu - map.width || !DataTarget(map.target, map.width) || map.target % map.width)
                return {false, "Mapping must be aligned data RAM, not executable/MMIO space."};
            for (const auto& old : mappings) {
                if (old.source == map.source || (old.target < uint64_t(map.target) + map.width && map.target < uint64_t(old.target) + old.width))
                    return {false, "Duplicate source or overlapping target mapping."};
            }
            if (mappings.size() >= kMaxInstructions)
                return {false, "Too many address mappings."};
            mappings.push_back(map);
        } else {
            return {false, "Unknown mapping directive: " + tag.substr(0, 32)};
        }
    }
    if (!header || !target || !source || mappings.empty())
        return {false, "Mapping requires header, source, target RMHP08 and explicit map rows."};
    return {};
}

uint64_t HashBytes(uint64_t hash, const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

CheatManager::Result ReadSmall(const std::filesystem::path& path, std::string& text) {
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {false, "Cannot open " + path.filename().string()};
        const auto size = in.tellg();
        if (size <= 0 || size > 65536)
            return {false, path.filename().string() + " must be 1 byte to 64 KiB."};
        text.resize(size_t(size));
        in.seekg(0);
        in.read(text.data(), std::streamsize(text.size()));
        return in ? CheatManager::Result{} : CheatManager::Result{false, "Short read: " + path.filename().string()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cannot read custom code package."};
    }
}

void PersistLocked() {
    if (!g_dirty || g_root.empty())
        return;
    try {
        const auto dir = g_root / "cheats";
        auto status = FsStatus::CreateDirectories(dir);
        if (!status.ok()) {
            g_status = status.message();
            return;
        }
        std::ostringstream out;
        out << "OCEANTRI-CUSTOM 1\ngame RMHP08\n";
        for (size_t i = 0; i < g_codes.size(); ++i)
            if (g_enabled[i])
                out << std::hex << g_codes[i].id << std::dec << " 1\n";
        const auto path = dir / "custom-active.oceantri";
        const auto temp = path.string() + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            const auto text = out.str();
            file.write(text.data(), std::streamsize(text.size()));
            file.close();
            if (!file) {
                g_status = "Cannot save custom-code switches (storage full/read-only).";
                return;
            }
        }
        status = FsStatus::Rename(temp, path);
        if (status.ok())
            g_dirty = false;
        else
            g_status = status.message();
    } catch (const std::exception& e) {
        g_status = e.what();
    } catch (...) {
        g_status = "Cannot save custom-code switches.";
    }
}

} // namespace

CheatManager::Result Parse(std::string_view text, std::string_view mapping, std::string_view fallbackName, Code& out) {
    try {
        if (text.size() > 65536 || mapping.size() > 65536)
            return {false, "Custom code or mapping exceeds 64 KiB."};
        Code parsed;
        std::vector<Mapping> maps;
        auto result = ParseMapping(mapping, parsed.sourceGame, maps);
        if (!result.ok)
            return result;
        const auto lines = Lines(text);
        if (lines.size() > 2048)
            return {false, "Custom code exceeds 2048 lines."};
        bool named = false;
        for (const auto line : lines) {
            if (line.empty() || line.front() == '*' || line.front() == '#')
                continue;
            if (line.front() == '$' || line.front() == '+') {
                if (named || !parsed.instructions.empty())
                    return {false, "Import one named code per .txt file; split multi-code lists."};
                parsed.name = std::string(Trim(line.substr(1)));
                const auto bracket = parsed.name.find('[');
                if (bracket != parsed.name.npos)
                    parsed.name = std::string(Trim(std::string_view(parsed.name).substr(0, bracket)));
                if (parsed.name.empty() || parsed.name.size() > 120)
                    return {false, "Code name must be 1 to 120 bytes."};
                named = true;
                continue;
            }
            std::istringstream row{std::string(line)};
            std::string a, b, extra;
            if (!(row >> a >> b) || (row >> extra) || a.size() != 8 || b.size() != 8)
                return {false, "Expected two 8-digit hex words per code line."};
            uint32_t address, data;
            if (!Hex(a, address) || !Hex(b, data))
                return {false, "Invalid hexadecimal code line."};
            // Gecko's leading byte selects 00/02/04 ordinary writes. Do not
            // reinterpret the remaining Gecko VM (conditionals, pointers,
            // serial/string writes, ASM, hooks or terminators) as RAM data.
            const uint32_t opcode = address >> 24;
            if (opcode != 0 && opcode != 2 && opcode != 4)
                return {false, "Unsupported Gecko instruction. Only non-repeat 8/16/32-bit RAM writes (00/02/04) are admitted; no ASM, pointers, conditionals or repeats."};
            const uint32_t subtype = opcode / 2;
            Instruction instruction;
            instruction.width = uint8_t(1u << subtype);
            instruction.source = 0x80000000u | (address & 0x01FFFFFFu);
            instruction.value = data;
            if (subtype == 0) {
                if ((data >> 16) != 0)
                    return {false, "Repeated byte writes are not supported."};
                instruction.value &= 0xFFu;
            } else if (subtype == 1) {
                if ((data >> 16) != 0)
                    return {false, "Repeated halfword writes are not supported."};
                instruction.value &= 0xFFFFu;
            } else if (instruction.source & 3) {
                return {false, "32-bit Gecko write address must be aligned."};
            }
            const auto map = std::find_if(maps.begin(), maps.end(), [&](const Mapping& m) { return m.source == instruction.source && m.width == instruction.width; });
            if (map == maps.end())
                return {false, "No exact reviewed PAL mapping for source address/width; automatic region offsets are forbidden."};
            instruction.target = map->target;
            for (const auto& old : parsed.instructions)
                if (old.target < uint64_t(instruction.target) + instruction.width && instruction.target < uint64_t(old.target) + old.width)
                    return {false, "Code writes overlap the same target bytes."};
            if (parsed.instructions.size() >= kMaxInstructions)
                return {false, "A custom code may contain at most 256 writes."};
            parsed.instructions.push_back(instruction);
        }
        if (parsed.instructions.empty())
            return {false, "Custom code contains no supported writes."};
        if (parsed.name.empty())
            parsed.name = std::string(fallbackName.substr(0, 120));
        if (parsed.name.empty())
            return {false, "Custom code needs a name."};
        uint64_t hash = 0xcbf29ce484222325ull;
        hash = HashBytes(hash, parsed.name.data(), parsed.name.size());
        hash = HashBytes(hash, parsed.sourceGame.data(), parsed.sourceGame.size());
        for (const auto& instruction : parsed.instructions) {
            hash = HashBytes(hash, &instruction.source, 4);
            hash = HashBytes(hash, &instruction.target, 4);
            hash = HashBytes(hash, &instruction.value, 4);
            hash = HashBytes(hash, &instruction.width, 1);
        }
        parsed.id = hash ? hash : 1;
        out = std::move(parsed);
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Invalid custom code package."};
    }
}

void Initialize(const std::filesystem::path& userDirectory) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        g_root = userDirectory;
    } catch (...) {
    }
    Refresh();
}

void Refresh() noexcept {
    std::vector<Code> codes;
    std::string status;
    std::set<uint64_t> ids;
    size_t total = 0, rejected = 0;
    std::filesystem::path root;
    try {
        {
            std::lock_guard lock(g_mutex);
            root = g_root;
        }
        const auto dir = root / "cheats" / "gecko";
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) {
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".txt")
                    continue;
                files.push_back(entry.path());
                if (files.size() > kMaxCodes)
                    break;
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                if (codes.size() >= kMaxCodes) {
                    ++rejected;
                    continue;
                }
                std::string text, mapping;
                auto mapPath = file;
                mapPath.replace_extension(".map");
                auto result = ReadSmall(file, text);
                if (result.ok)
                    result = ReadSmall(mapPath, mapping);
                Code code;
                if (result.ok)
                    result = Parse(text, mapping, file.stem().string(), code);
                if (result.ok && (ids.count(code.id) || total + code.instructions.size() > kMaxTotalInstructions))
                    result = {false, "Duplicate code identity or total write budget exceeded."};
                if (!result.ok) {
                    ++rejected;
                    if (status.empty())
                        status = file.filename().string() + ": " + result.message;
                    continue;
                }
                code.file = file;
                ids.insert(code.id);
                total += code.instructions.size();
                codes.push_back(std::move(code));
            }
        }
        std::vector<uint8_t> enabled(codes.size(), 0);
        std::string saved;
        const auto active = root / "cheats" / "custom-active.oceantri";
        if (std::filesystem::exists(active, ec) && ReadSmall(active, saved).ok) {
            bool valid = true, header = false, game = false;
            for (const auto line : Lines(saved)) {
                if (line.empty() || line.front() == '#')
                    continue;
                std::istringstream row{std::string(line)};
                std::string a, b, c;
                if (!header && line == "OCEANTRI-CUSTOM 1") { header = true; continue; }
                if (header && !game && line == "game RMHP08") { game = true; continue; }
                if (!header || !game) { valid = false; break; }
                uint64_t id;
                if (!(row >> std::hex >> id >> std::dec >> b) || (row >> c) || b != "1") {
                    valid = false;
                    break;
                }
                const auto it = std::find_if(codes.begin(), codes.end(), [&](const Code& code) { return code.id == id; });
                if (it == codes.end() || enabled[size_t(it - codes.begin())]) {
                    valid = false;
                    break;
                }
                enabled[size_t(it - codes.begin())] = 1;
            }
            if (!valid || !header || !game) {
                std::fill(enabled.begin(), enabled.end(), 0);
                status = "Saved custom-code switches rejected; all custom codes OFF. " + status;
            }
        }
        if (status.empty())
            status = std::to_string(codes.size()) + " mapped code(s), " + std::to_string(total) + " RAM write(s). OFF stops writes; it does not restore previous RAM.";
        else if (rejected)
            status = std::to_string(rejected) + " package(s) rejected. " + status;
        std::lock_guard lock(g_mutex);
        g_codes = std::move(codes);
        g_enabled = std::move(enabled);
        g_pending.clear();
        g_status = std::move(status);
        g_dirty = false;
    } catch (const std::exception& e) {
        std::lock_guard lock(g_mutex);
        g_codes.clear();
        g_enabled.clear();
        g_pending.clear();
        g_status = std::string("Custom import scan failed: ") + e.what();
    } catch (...) {
        std::lock_guard lock(g_mutex);
        g_codes.clear();
        g_enabled.clear();
        g_pending.clear();
        g_status = "Custom import scan failed; all custom codes OFF.";
    }
}

View GetView() noexcept {
    View view;
    try {
        std::lock_guard lock(g_mutex);
        view.codes = g_codes;
        view.enabled = g_enabled;
        view.status = g_status;
    } catch (...) {
        view.status = "Custom-code state unavailable.";
    }
    return view;
}

bool QueueToggle(uint64_t id, bool enabled) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        if (g_pending.size() >= 64 || !std::any_of(g_codes.begin(), g_codes.end(), [&](const Code& code) { return code.id == id; }))
            return false;
        g_pending.emplace_back(id, enabled);
        return true;
    } catch (...) {
        return false;
    }
}

void ApplyEnabled(const std::vector<uint64_t>& ids) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        std::fill(g_enabled.begin(), g_enabled.end(), 0);
        for (const auto id : ids)
            for (size_t i = 0; i < g_codes.size(); ++i)
                if (g_codes[i].id == id)
                    g_enabled[i] = 1;
        g_pending.clear();
        g_dirty = true;
    } catch (...) {
    }
}

std::vector<uint64_t> EnabledIds() noexcept {
    std::vector<uint64_t> ids;
    try {
        std::lock_guard lock(g_mutex);
        for (size_t i = 0; i < g_codes.size(); ++i)
            if (g_enabled[i])
                ids.push_back(g_codes[i].id);
    } catch (...) {
    }
    return ids;
}

void Pump() noexcept {
    try {
        std::lock_guard lock(g_mutex);
        for (const auto& [id, enabled] : g_pending)
            for (size_t i = 0; i < g_codes.size(); ++i)
                if (g_codes[i].id == id)
                    g_enabled[i] = enabled;
        if (!g_pending.empty()) {
            g_pending.clear();
            g_dirty = true;
            g_status = "Custom switches applied on the game thread. Review explicit PAL mappings before use.";
        }
        PersistLocked();
    } catch (...) {
    }
}

void Step(const CpuContext*) noexcept {
    if (!Cheats::g_active.load(std::memory_order_relaxed) || OnlineSession::Active())
        return;
    try {
        std::lock_guard lock(g_mutex);
        bool any = false;
        for (size_t i = 0; i < g_codes.size(); ++i) {
            if (!g_enabled[i])
                continue;
            for (const auto& instruction : g_codes[i].instructions) {
                if (!Memory::Contains(instruction.target, instruction.width))
                    continue;
                switch (instruction.width) {
                case 1:
                    if (Memory::Read8(instruction.target) != instruction.value)
                        Memory::Write8(instruction.target, uint8_t(instruction.value));
                    break;
                case 2:
                    if (Memory::Read16(instruction.target) != instruction.value)
                        Memory::Write16(instruction.target, uint16_t(instruction.value));
                    break;
                case 4:
                    if (Memory::Read32(instruction.target) != instruction.value)
                        Memory::Write32(instruction.target, instruction.value);
                    break;
                }
                any = true;
            }
        }
        if (any)
            Cheats::g_used.store(true, std::memory_order_relaxed);
    } catch (...) {
        // Imported data must never turn a bad allocation into a host crash.
    }
}

void ResetForTests() noexcept {
    std::lock_guard lock(g_mutex);
    g_root.clear();
    g_codes.clear();
    g_enabled.clear();
    g_pending.clear();
    g_dirty = false;
    g_status = "No mapped custom codes imported.";
}

} // namespace CustomCodes
