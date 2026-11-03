#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>


// Runtime, read-only asset packages. Original disc/save files are never
// edited. Texture roots are selected on the GL thread; BRRES/audio file
// replacements are installed once before the asset cache is constructed and
// therefore apply at the next launch. Converted caches are namespaced by the
// exact active replacement hashes so an unmodded pack can never be reused.
namespace ModManager {

inline constexpr size_t kMaxPackages = 128;
inline constexpr size_t kMaxEntries = 256;
inline constexpr uint64_t kMaxFileBytes = 64ull * 1024 * 1024;
inline constexpr uint64_t kMaxActiveFileBytes = 128ull * 1024 * 1024;

enum class EntryKind : uint8_t { Model, Audio };

struct Entry {
    EntryKind kind = EntryKind::Model;
    std::string target; // FST path, e.g. 15/.../weapon.brres or 16/srt/...
    std::filesystem::path local;
    uint64_t bytes = 0;
    std::string sha256;
};

struct Package {
    std::string id, name, version, author, description;
    int priority = 0;
    std::filesystem::path directory;
    std::filesystem::path textureRoot; // empty when the package has no HD textures
    size_t textureFiles = 0;
    std::vector<Entry> entries;
    bool valid = false;
    bool active = false;       // desired configuration
    bool fileApplied = false;  // immutable session snapshot for file entries
    bool filePending = false;  // desired file state differs from session snapshot
    bool conflict = false;
    std::string error;
};

struct View {
    std::vector<Package> packages;
    std::string status;
    uint64_t revision = 0;
};

struct Result {
    bool ok = true;
    std::string message;
};

Result ParseManifest(std::string_view text, const std::filesystem::path& directory, Package& out);
Result Sha256File(const std::filesystem::path& path, uint64_t expectedBytes, std::string& hex);

void Initialize(const std::filesystem::path& userDirectory) noexcept;
void Refresh() noexcept;
View GetView() noexcept;
bool QueueToggle(std::string_view id, bool active) noexcept;
bool QueueRefresh() noexcept;
void Pump() noexcept; // game thread

// GL thread: applies managed texture roots only when the selection changed.
void ApplyTextureSelection() noexcept;
std::vector<std::filesystem::path> TextureRoots() noexcept;
uint64_t TextureRevision() noexcept;

// A small read-only FST view, so the overlay/cache policy is testable without
// a licensed disc image. Called once before any guest read.
struct DiscView {
    bool nativeTextureMap = false;
    std::function<uint32_t(const std::string&)> fileSize;
    std::function<uint64_t(const std::string&)> fileOffset;
};
// Returns the asset-cache root (original, or a hash-namespaced mod cache).
std::filesystem::path PrepareDiscCache(const DiscView& image, const std::filesystem::path& originalRoot) noexcept;
bool HasAppliedFiles() noexcept;
// Output already holds the original partition bytes; replacement bytes are
// overlaid in memory only. Called by the asset-cache raw reader / extracted
// reader, never by the UI thread.
void OverlayRead(uint64_t offset, uint8_t* output, size_t length) noexcept;
void ResetForTests() noexcept;

} // namespace ModManager
