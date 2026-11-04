#include "mod_manager.h"

#include "fs_status.h"
#if defined(MH3_HAS_AURORA)
// Declared here rather than including Aurora's internal header: the runtime
// target does not add aurora-main/lib to every source's include path.
namespace aurora::gfx::texture_replacement {
void set_managed_roots(std::vector<std::filesystem::path> roots) noexcept;
void reload() noexcept;
}
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

namespace ModManager {
namespace {

struct AppliedFile {
    uint64_t offset = 0, size = 0;
    std::shared_ptr<const std::vector<uint8_t>> data;
};

struct Toggle {
    std::string id;
    bool active = false;
};

std::mutex g_mutex;
std::filesystem::path g_root;
std::vector<Package> g_packages;
std::vector<Toggle> g_pending;
std::vector<AppliedFile> g_applied; // immutable after PrepareDiscCache
std::set<std::string> g_appliedIds;
std::string g_status = "No runtime mod packages imported.";
std::atomic<uint64_t> g_revision{0};
std::atomic<uint64_t> g_textureRevision{0};
bool g_refresh = false;
bool g_dirty = false;

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

bool SafeRelative(std::string_view text) {
    if (text.empty() || text.size() > 240 || text.front() == '/' || text.find('\\') != text.npos || text.find(':') != text.npos)
        return false;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('/', at);
        const auto part = text.substr(at, end == text.npos ? text.npos : end - at);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (end == text.npos)
            break;
        at = end + 1;
    }
    return true;
}

bool Under(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto a = path.begin();
    auto b = root.begin();
    for (; b != root.end(); ++b, ++a)
        if (a == path.end() || *a != *b)
            return false;
    return true;
}

Result SafePath(const std::filesystem::path& base, std::string_view relative, std::filesystem::path& out) {
    if (!SafeRelative(relative))
        return {false, "Unsafe relative package path."};
    try {
        const auto root = std::filesystem::weakly_canonical(base);
        out = std::filesystem::weakly_canonical(root / std::filesystem::path(std::string(relative)));
        if (!Under(out, root))
            return {false, "Package path escapes its directory."};
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cannot resolve package path."};
    }
}

bool HexHash(std::string_view text) {
    if (text.size() != 64)
        return false;
    for (const char c : text)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

bool Decimal(std::string_view text, uint64_t& value) {
    if (text.empty())
        return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::string Lower(std::string s) {
    for (auto& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ValidId(std::string_view id) {
    if (id.empty() || id.size() > 64)
        return false;
    for (const char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
            return false;
    return true;
}

Result ReadSmall(const std::filesystem::path& path, std::string& text) {
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {false, "Missing package.oceantri manifest."};
        const auto size = in.tellg();
        if (size <= 0 || size > 65536)
            return {false, "Mod manifest must be 1 byte to 64 KiB."};
        text.resize(size_t(size));
        in.seekg(0);
        in.read(text.data(), std::streamsize(text.size()));
        return in ? Result{} : Result{false, "Short read of mod manifest."};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cannot read mod manifest."};
    }
}

// Small, dependency-free SHA-256 for package integrity (not a security token).
struct Sha256 {
    std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block{};
    uint64_t bytes = 0;
    size_t used = 0;

    static uint32_t Ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void Transform(const uint8_t* p) {
        static constexpr uint32_t k[64]{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<uint32_t, 64> w{};
        for (unsigned i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4*i]) << 24 | uint32_t(p[4*i+1]) << 16 | uint32_t(p[4*i+2]) << 8 | p[4*i+3];
        for (unsigned i = 16; i < 64; ++i) {
            const auto s0 = Ror(w[i-15],7) ^ Ror(w[i-15],18) ^ (w[i-15] >> 3);
            const auto s1 = Ror(w[i-2],17) ^ Ror(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto [a,b,c,d,e,f,g,hh] = h;
        for (unsigned i = 0; i < 64; ++i) {
            const auto s1 = Ror(e,6) ^ Ror(e,11) ^ Ror(e,25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = hh + s1 + ch + k[i] + w[i];
            const auto s0 = Ror(a,2) ^ Ror(a,13) ^ Ror(a,22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    void Update(const uint8_t* p, size_t n) {
        bytes += n;
        while (n) {
            const size_t take = std::min(n, 64 - used);
            std::memcpy(block.data() + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { Transform(block.data()); used = 0; }
        }
    }
    std::string Finish() {
        const uint64_t bits = bytes * 8;
        const uint8_t one = 0x80, zero = 0;
        Update(&one, 1);
        while (used != 56)
            Update(&zero, 1);
        uint8_t length[8];
        for (unsigned i = 0; i < 8; ++i)
            length[i] = uint8_t(bits >> (56 - 8*i));
        // Append the length without counting it as message bytes.
        std::memcpy(block.data() + 56, length, 8);
        Transform(block.data());
        static constexpr char hex[] = "0123456789abcdef";
        std::string out(64, '0');
        for (unsigned i = 0; i < 8; ++i)
            for (unsigned j = 0; j < 4; ++j) {
                out[8*i+2*j] = hex[(h[i] >> (28 - 8*j)) & 15];
                out[8*i+2*j+1] = hex[(h[i] >> (24 - 8*j)) & 15];
            }
        return out;
    }
};

void OrderPackages(std::vector<Package>& packages) {
    std::sort(packages.begin(), packages.end(), [](const Package& a, const Package& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
}

void PersistLocked() {
    if (!g_dirty || g_root.empty())
        return;
    try {
        const auto dir = g_root / "mods";
        auto status = FsStatus::CreateDirectories(dir);
        if (!status.ok()) { g_status = status.message(); return; }
        std::ostringstream out;
        out << "OCEANTRI-MODS 1\ngame RMHP08\n";
        for (const auto& package : g_packages)
            if (package.active)
                out << "mod " << package.id << " 1\n";
        const auto path = dir / "active.oceantri";
        const auto temp = path.string() + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            const auto text = out.str();
            file.write(text.data(), std::streamsize(text.size()));
            file.close();
            if (!file) { g_status = "Cannot save mod activation (storage full/read-only)."; return; }
        }
        status = FsStatus::Rename(temp, path);
        if (status.ok()) g_dirty = false;
        else g_status = status.message();
    } catch (const std::exception& e) {
        g_status = e.what();
    } catch (...) {
        g_status = "Cannot save mod activation.";
    }
}

} // namespace

Result Sha256File(const std::filesystem::path& path, uint64_t expectedBytes, std::string& hex) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {false, "Cannot open replacement file."};
        Sha256 hash;
        std::vector<uint8_t> buffer(65536);
        uint64_t total = 0;
        while (in) {
            in.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
            const auto got = in.gcount();
            if (got > 0) {
                total += uint64_t(got);
                if (total > expectedBytes)
                    return {false, "Replacement file grew while hashing."};
                hash.Update(buffer.data(), size_t(got));
            }
        }
        if (!in.eof() || total != expectedBytes)
            return {false, "Replacement size changed or read failed."};
        hex = hash.Finish();
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cannot hash replacement file."};
    }
}

Result ParseManifest(std::string_view text, const std::filesystem::path& directory, Package& out) {
    try {
        if (text.empty() || text.size() > 65536)
            return {false, "Mod manifest must be 1..65536 bytes."};
        Package parsed;
        parsed.directory = directory;
        bool header = false, game = false, id = false, name = false, textures = false;
        std::istringstream in{std::string(text)};
        std::string line;
        unsigned lines = 0;
        while (std::getline(in, line)) {
            if (++lines > 2048)
                return {false, "Mod manifest exceeds 2048 lines."};
            const auto trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#')
                continue;
            std::istringstream row{std::string(trimmed)};
            std::string tag;
            row >> tag;
            auto rest = [&] { std::string s; std::getline(row >> std::ws, s); return std::string(Trim(s)); };
            if (tag == "OCEANTRI-MOD") {
                std::string v, extra;
                if (header || !(row >> v) || v != "1" || (row >> extra))
                    return {false, "Expected OCEANTRI-MOD 1."};
                header = true;
            } else if (tag == "game") {
                std::string v, extra;
                if (game || !(row >> v) || v != "RMHP08" || (row >> extra))
                    return {false, "Mod packages must target PAL RMHP08."};
                game = true;
            } else if (tag == "id") {
                auto v = rest();
                if (id || !ValidId(v) || v != directory.filename().string())
                    return {false, "Package id must be safe and match its directory name."};
                parsed.id = v; id = true;
            } else if (tag == "name") {
                auto v = rest();
                if (name || v.empty() || v.size() > 120)
                    return {false, "Package name must be 1..120 bytes."};
                parsed.name = v; name = true;
            } else if (tag == "version") {
                auto v = rest();
                if (!parsed.version.empty() || v.size() > 40)
                    return {false, "Invalid version."};
                parsed.version = v;
            } else if (tag == "author") {
                auto v = rest();
                if (!parsed.author.empty() || v.size() > 120)
                    return {false, "Invalid author."};
                parsed.author = v;
            } else if (tag == "description") {
                auto v = rest();
                if (!parsed.description.empty() || v.size() > 500)
                    return {false, "Invalid description."};
                parsed.description = v;
            } else if (tag == "priority") {
                std::string v, extra;
                int value;
                if (!(row >> v) || (row >> extra) || v.size() > 6)
                    return {false, "Invalid priority."};
                const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), value);
                if (error != std::errc{} || end != v.data() + v.size() || value < -1000 || value > 1000)
                    return {false, "Priority must be -1000..1000."};
                parsed.priority = value;
            } else if (tag == "textures") {
                auto v = rest();
                if (textures)
                    return {false, "Only one managed texture root per package."};
                std::filesystem::path root;
                auto result = SafePath(directory, v, root);
                if (!result.ok) return result;
                std::error_code ec;
                if (!std::filesystem::is_directory(root, ec) || std::filesystem::is_symlink(root, ec))
                    return {false, "Texture root must be a real directory inside the package."};
                size_t count = 0;
                for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                    if (ec) return {false, "Cannot scan texture root."};
                    if (it->is_symlink(ec)) return {false, "Texture symlinks are not admitted."};
                    if (!it->is_regular_file(ec)) continue;
                    const auto ext = Lower(it->path().extension().string());
                    if (ext != ".png" && ext != ".dds") continue;
                    if (++count > 4096) return {false, "Texture root exceeds 4096 images."};
                }
                if (!count) return {false, "Texture root contains no PNG/DDS replacements."};
                parsed.textureRoot = root; parsed.textureFiles = count; textures = true;
            } else if (tag == "file") {
                std::string target, local, hash, sizeText, extra;
                uint64_t bytes;
                if (!(row >> target >> local >> hash >> sizeText) || (row >> extra) || !SafeRelative(target) || !HexHash(hash) || !Decimal(sizeText, bytes) || bytes == 0 || bytes > kMaxFileBytes)
                    return {false, "Expected file <FST target> <local path> <sha256> <bytes>."};
                const bool audio = target.rfind("16/srt/", 0) == 0;
                if (!audio && Lower(std::filesystem::path(target).extension().string()) != ".brres")
                    return {false, "Runtime file replacements are limited to BRRES models/textures and 16/srt audio streams."};
                std::filesystem::path path;
                auto result = SafePath(directory, local, path);
                if (!result.ok) return result;
                std::error_code ec;
                if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::is_symlink(path, ec) || std::filesystem::file_size(path, ec) != bytes)
                    return {false, "Replacement file missing, symlinked or wrong size: " + local};
                for (const auto& old : parsed.entries)
                    if (old.target == target)
                        return {false, "Duplicate replacement target."};
                if (parsed.entries.size() >= kMaxEntries)
                    return {false, "Package exceeds 256 file replacements."};
                Entry entry;
                entry.kind = audio ? EntryKind::Audio : EntryKind::Model;
                entry.target = target; entry.local = path; entry.bytes = bytes; entry.sha256 = Lower(hash);
                parsed.entries.push_back(std::move(entry));
            } else {
                return {false, "Unknown manifest field: " + tag.substr(0, 32)};
            }
        }
        if (!header || !game || !id || !name)
            return {false, "Manifest requires header, game RMHP08, id and name."};
        if (!textures && parsed.entries.empty())
            return {false, "Package has no texture root or file replacements."};
        parsed.valid = true;
        out = std::move(parsed);
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Invalid mod manifest."};
    }
}

void Initialize(const std::filesystem::path& userDirectory) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        g_root = userDirectory;
    } catch (...) {}
    Refresh();
}

void Refresh() noexcept {
    std::vector<Package> packages;
    std::string status;
    size_t rejected = 0;
    try {
        std::filesystem::path root;
        std::set<std::string> appliedIds;
        { std::lock_guard lock(g_mutex); root = g_root; appliedIds = g_appliedIds; }
        const auto dir = root / "mods";
        std::error_code ec;
        std::vector<std::filesystem::path> dirs;
        if (std::filesystem::is_directory(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_directory(ec) || entry.path().filename() == "mod-cache" || entry.is_symlink(ec))
                    continue;
                dirs.push_back(entry.path());
                if (dirs.size() > kMaxPackages) break;
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::set<std::string> ids;
        for (const auto& path : dirs) {
            if (packages.size() >= kMaxPackages) { ++rejected; continue; }
            std::string text;
            auto result = ReadSmall(path / "package.oceantri", text);
            Package package;
            if (result.ok) result = ParseManifest(text, path, package);
            if (result.ok && ids.count(package.id)) result = {false, "Duplicate package id."};
            if (result.ok) {
                for (const auto& entry : package.entries) {
                    std::string hash;
                    result = Sha256File(entry.local, entry.bytes, hash);
                    if (!result.ok) break;
                    if (hash != entry.sha256) { result = {false, "SHA-256 mismatch: " + entry.target}; break; }
                }
            }
            if (!result.ok) {
                ++rejected;
                package.valid = false;
                package.error = result.message;
                if (status.empty()) status = path.filename().string() + ": " + result.message;
                // Keep invalid packages visible but never activatable.
                package.id = path.filename().string();
                if (package.name.empty()) package.name = package.id;
                package.directory = path;
                package.entries.clear(); package.textureRoot.clear();
                packages.push_back(std::move(package));
                continue;
            }
            ids.insert(package.id);
            packages.push_back(std::move(package));
        }
        OrderPackages(packages);
        std::set<std::string> active;
        const auto activePath = dir / "active.oceantri";
        std::string saved;
        if (std::filesystem::exists(activePath, ec) && ReadSmall(activePath, saved).ok) {
            bool valid = true, header = false, game = false;
            std::istringstream in(saved);
            std::string line;
            while (std::getline(in, line)) {
                const auto t = Trim(line);
                if (t.empty() || t.front() == '#') continue;
                if (!header && t == "OCEANTRI-MODS 1") { header = true; continue; }
                if (header && !game && t == "game RMHP08") { game = true; continue; }
                if (!header || !game) { valid = false; break; }
                std::istringstream row{std::string(t)};
                std::string tag, id, value, extra;
                if (!(row >> tag >> id >> value) || (row >> extra) || tag != "mod" || value != "1" || active.count(id)) { valid = false; break; }
                bool known = false;
                for (const auto& package : packages) known |= package.id == id;
                if (!known) { valid = false; break; }
                active.insert(id);
            }
            if (!valid || !header || !game) { active.clear(); status = "Saved mod activation rejected; packages OFF. " + status; }
        }
        std::set<std::string> claimed;
        for (auto& package : packages) {
            package.active = package.valid && active.count(package.id);
            package.fileApplied = appliedIds.count(package.id) != 0;
            package.filePending = !package.entries.empty() && package.active != package.fileApplied;
            if (package.active) {
                for (const auto& entry : package.entries) {
                    if (!claimed.insert(entry.target).second) {
                        package.conflict = true;
                        package.error = "A higher-priority active package already replaces " + entry.target;
                    }
                }
            }
        }
        if (status.empty()) {
            size_t activeCount = 0, textures = 0, files = 0;
            for (const auto& p : packages) if (p.active) { ++activeCount; textures += p.textureFiles; files += p.entries.size(); }
            status = std::to_string(activeCount) + " active package(s), " + std::to_string(textures) + " managed texture image(s), " + std::to_string(files) + " file replacement(s). Model/audio changes apply at next launch.";
        } else if (rejected) {
            status = std::to_string(rejected) + " package(s) rejected. " + status;
        }
        std::lock_guard lock(g_mutex);
        g_packages = std::move(packages);
        g_status = std::move(status);
        g_pending.clear();
        g_refresh = false;
        g_revision.fetch_add(1, std::memory_order_release);
        g_textureRevision.fetch_add(1, std::memory_order_release);
    } catch (const std::exception& e) {
        std::lock_guard lock(g_mutex);
        g_status = std::string("Mod scan failed: ") + e.what();
    } catch (...) {
        std::lock_guard lock(g_mutex);
        g_status = "Mod scan failed; original assets remain selected.";
    }
}

View GetView() noexcept {
    View view;
    try {
        std::lock_guard lock(g_mutex);
        view.packages = g_packages;
        for (auto& p : view.packages) {
            p.fileApplied = g_appliedIds.count(p.id) != 0;
            p.filePending = !p.entries.empty() && p.active != p.fileApplied;
        }
        view.status = g_status;
        view.revision = g_revision.load(std::memory_order_acquire);
    } catch (...) {
        view.status = "Mod manager state unavailable.";
    }
    return view;
}

bool QueueToggle(std::string_view id, bool active) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        if (g_pending.size() >= 64)
            return false;
        const auto it = std::find_if(g_packages.begin(), g_packages.end(), [&](const Package& p) { return p.id == id; });
        if (it == g_packages.end() || !it->valid)
            return false;
        g_pending.push_back({std::string(id), active});
        return true;
    } catch (...) { return false; }
}

bool QueueRefresh() noexcept {
    try {
        std::lock_guard lock(g_mutex);
        if (g_refresh) return true;
        g_refresh = true;
        return true;
    } catch (...) { return false; }
}

void Pump() noexcept {
    bool refresh = false;
    try {
        {
            std::lock_guard lock(g_mutex);
            refresh = g_refresh;
            for (const auto& toggle : g_pending)
                for (auto& package : g_packages)
                    if (package.id == toggle.id && package.valid)
                        package.active = toggle.active;
            if (!g_pending.empty()) {
                g_pending.clear();
                g_dirty = true;
                g_revision.fetch_add(1, std::memory_order_release);
                g_textureRevision.fetch_add(1, std::memory_order_release);
                g_status = "Mod selection saved. Textures reload now; model/audio file replacements apply at next launch.";
            }
            PersistLocked();
        }
        if (refresh)
            Refresh();
    } catch (...) {}
}

std::vector<std::filesystem::path> TextureRoots() noexcept {
    std::vector<std::filesystem::path> roots;
    try {
        std::lock_guard lock(g_mutex);
        auto packages = g_packages;
        OrderPackages(packages);
        for (const auto& package : packages)
            if (package.active && package.valid && !package.textureRoot.empty())
                roots.push_back(package.textureRoot);
    } catch (...) {}
    return roots;
}

uint64_t TextureRevision() noexcept { return g_textureRevision.load(std::memory_order_acquire); }

void ApplyTextureSelection() noexcept {
#if defined(MH3_HAS_AURORA)
    static uint64_t applied = UINT64_MAX;
    static bool hadManagedRoots = false;
    const auto revision = TextureRevision();
    if (revision == applied)
        return;
    auto roots = TextureRoots();
    const bool hasManagedRoots = !roots.empty();
    // With no managed packages, do not touch Aurora's replacement index at
    // startup. The legacy fixed-root behavior must remain exactly as before.
    if (!hasManagedRoots && !hadManagedRoots) {
        applied = revision;
        return;
    }
    aurora::gfx::texture_replacement::set_managed_roots(std::move(roots));
    aurora::gfx::texture_replacement::reload();
    hadManagedRoots = hasManagedRoots;
    applied = revision;
#endif
}

std::filesystem::path PrepareDiscCache(const DiscView& image, const std::filesystem::path& originalRoot) noexcept {
    try {
        std::lock_guard lock(g_mutex);
        g_applied.clear();
        g_appliedIds.clear();
        if (image.nativeTextureMap || std::getenv("MH3_NATIVE_ASSET_DIR")) {
            g_status = "Prepared native assets bypass runtime BRRES/audio overlays. Disable prepared native assets or reprepare them with the mods; texture packs still work.";
            return originalRoot;
        }
        auto packages = g_packages;
        OrderPackages(packages);
        std::set<std::string> targets;
        uint64_t total = 0;
        std::vector<AppliedFile> applied;
        std::set<std::string> appliedIds;
        std::string warning;
        for (const auto& package : packages) {
            if (!package.active || !package.valid || package.entries.empty())
                continue;
            std::vector<AppliedFile> pending;
            std::set<std::string> packageTargets;
            bool ok = true;
            uint64_t packageBytes = 0;
            for (const auto& entry : package.entries) {
                const uint32_t original = image.fileSize ? image.fileSize(entry.target) : 0;
                if (!original || original != entry.bytes || targets.count(entry.target)) {
                    warning = package.id + ": target missing, conflicting or not the exact original length: " + entry.target;
                    ok = false; break;
                }
                std::string hash;
                auto result = Sha256File(entry.local, entry.bytes, hash);
                if (!result.ok || hash != entry.sha256) {
                    warning = package.id + ": replacement changed or failed integrity check: " + entry.target;
                    ok = false; break;
                }
                if (packageBytes + entry.bytes > kMaxActiveFileBytes || total + packageBytes + entry.bytes > kMaxActiveFileBytes) {
                    warning = "Active file replacements exceed the 128 MiB memory overlay budget.";
                    ok = false; break;
                }
                auto data = std::make_shared<std::vector<uint8_t>>(size_t(entry.bytes));
                std::ifstream in(entry.local, std::ios::binary);
                in.read(reinterpret_cast<char*>(data->data()), std::streamsize(data->size()));
                if (!in) { warning = package.id + ": cannot read " + entry.target; ok = false; break; }
                if (entry.kind == EntryKind::Model) {
                    if (data->size() < 16 || std::memcmp(data->data(), "bres", 4) != 0) {
                        warning = package.id + ": replacement is not a BRRES container: " + entry.target;
                        ok = false; break;
                    }
                }
                AppliedFile file;
                file.offset = image.fileOffset ? image.fileOffset(entry.target) : 0;
                file.size = entry.bytes;
                file.data = std::move(data);
                pending.push_back(std::move(file));
                packageBytes += entry.bytes;
                packageTargets.insert(entry.target);
            }
            if (!ok) {
                for (auto& stored : g_packages)
                    if (stored.id == package.id) { stored.error = warning; stored.conflict = true; }
                continue;
            }
            total += packageBytes;
            targets.insert(packageTargets.begin(), packageTargets.end());
            appliedIds.insert(package.id);
            for (auto& file : pending)
                applied.push_back(std::move(file));
        }
        if (applied.empty()) {
            if (!warning.empty()) g_status = warning;
            return originalRoot;
        }
        std::sort(applied.begin(), applied.end(), [](const AppliedFile& a, const AppliedFile& b) { return a.offset < b.offset; });
        uint64_t key = 0xcbf29ce484222325ull;
        for (const auto& package : packages) {
            if (!appliedIds.count(package.id)) continue;
            for (const char c : package.id) { key ^= uint8_t(c); key *= 0x100000001b3ull; }
            for (const auto& entry : package.entries)
                for (const char c : entry.sha256) { key ^= uint8_t(c); key *= 0x100000001b3ull; }
        }
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(key));
        const auto cacheRoot = g_root / "mod-cache" / name;
        const auto status = FsStatus::CreateDirectories(cacheRoot);
        if (!status.ok()) {
            g_status = "Cannot create mod asset cache; original assets kept: " + status.message();
            return originalRoot;
        }
        g_applied = std::move(applied);
        g_appliedIds = std::move(appliedIds);
        for (auto& package : g_packages) {
            package.fileApplied = g_appliedIds.count(package.id) != 0;
            package.filePending = !package.entries.empty() && package.active != package.fileApplied;
        }
        g_status = warning.empty() ? "Runtime file overlays installed without editing the disc. Converted assets use a separate hash-named cache." : warning;
        g_revision.fetch_add(1, std::memory_order_release);
        return cacheRoot;
    } catch (const std::exception& e) {
        std::lock_guard lock(g_mutex);
        g_applied.clear();
        g_appliedIds.clear();
        g_status = std::string("Runtime file overlay failed; original assets kept: ") + e.what();
        return originalRoot;
    } catch (...) {
        std::lock_guard lock(g_mutex);
        g_applied.clear();
        g_appliedIds.clear();
        g_status = "Runtime file overlay failed; original assets kept.";
        return originalRoot;
    }
}

bool HasAppliedFiles() noexcept {
    try { std::lock_guard lock(g_mutex); return !g_applied.empty(); } catch (...) { return false; }
}

void OverlayRead(uint64_t offset, uint8_t* output, size_t length) noexcept {
    if (!output || !length)
        return;
    try {
        std::lock_guard lock(g_mutex);
        const uint64_t end = offset + length;
        for (const auto& file : g_applied) {
            if (file.offset >= end)
                break;
            const uint64_t stop = file.offset + file.size;
            if (stop <= offset)
                continue;
            const uint64_t from = std::max(offset, file.offset);
            const uint64_t to = std::min(end, stop);
            std::memcpy(output + (from - offset), file.data->data() + (from - file.offset), size_t(to - from));
        }
    } catch (...) {
        // A failed overlay leaves the original bytes already read in place.
    }
}

void ResetForTests() noexcept {
    std::lock_guard lock(g_mutex);
    g_root.clear();
    g_packages.clear();
    g_pending.clear();
    g_applied.clear();
    g_appliedIds.clear();
    g_status = "No runtime mod packages imported.";
    g_refresh = false;
    g_dirty = false;
    g_revision.fetch_add(1, std::memory_order_release);
    g_textureRevision.fetch_add(1, std::memory_order_release);
}

} // namespace ModManager
