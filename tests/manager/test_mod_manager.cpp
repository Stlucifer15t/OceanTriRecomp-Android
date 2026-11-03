#include "mod_manager.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), std::streamsize(text.size()));
    Check(bool(out), "write fixture");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
std::string Manifest(std::string_view id, std::string_view hash, uint64_t bytes, int priority = 0) {
    return "OCEANTRI-MOD 1\ngame RMHP08\nid " + std::string(id) + "\nname Test package\npriority " + std::to_string(priority) +
           "\ntextures textures\nfile 15/test.brres files/test.brres " + std::string(hash) + " " + std::to_string(bytes) + "\n";
}
}

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / ("oceantri-mods-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto packageDir = root / "mods" / "test-pack";
        std::string brres(64, '\0');
        std::memcpy(brres.data(), "bres", 4);
        Write(packageDir / "files" / "test.brres", brres);
        Write(packageDir / "textures" / "RMHP08_test.png", "not-decoded-by-manager");
        std::string hash;
        Check(ModManager::Sha256File(packageDir / "files" / "test.brres", brres.size(), hash).ok, "sha256 file");
        Check(hash.size() == 64, "sha256 hex");
        const auto abcPath = root / "abc.txt";
        Write(abcPath, "abc");
        std::string abcHash;
        Check(ModManager::Sha256File(abcPath, 3, abcHash).ok && abcHash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
        const auto manifest = Manifest("test-pack", hash, brres.size());
        Write(packageDir / "package.oceantri", manifest);

        ModManager::Package parsed;
        auto result = ModManager::ParseManifest(manifest, packageDir, parsed);
        Check(result.ok, result.message.c_str());
        Check(parsed.id == "test-pack" && parsed.entries.size() == 1 && parsed.textureFiles == 1, "manifest entries");
        Check(!ModManager::ParseManifest(Manifest("other-id", hash, brres.size()), packageDir, parsed).ok, "directory/id mismatch rejected");
        Check(!ModManager::ParseManifest(manifest + "exec 80000000\n", packageDir, parsed).ok, "unknown executable field rejected");
        auto escape = manifest;
        escape.replace(escape.find("files/test.brres"), 16, "../escape.brres");
        Check(!ModManager::ParseManifest(escape, packageDir, parsed).ok, "path escape rejected");
        auto wrongRegion = manifest;
        wrongRegion.replace(wrongRegion.find("RMHP08"), 6, "RMHE08");
        Check(!ModManager::ParseManifest(wrongRegion, packageDir, parsed).ok, "wrong region rejected");

        ModManager::ResetForTests();
        ModManager::Initialize(root);
        auto view = ModManager::GetView();
        Check(view.packages.size() == 1 && view.packages[0].valid && !view.packages[0].active, "valid package starts OFF");
        Check(ModManager::QueueToggle("test-pack", true), "queue activation");
        Check(!ModManager::GetView().packages[0].active, "UI queue does not mutate selection");
        ModManager::Pump();
        Check(ModManager::GetView().packages[0].active, "game-thread pump activates");
        Check(ModManager::TextureRoots().size() == 1, "managed texture root selected");
        Check(Read(root / "mods" / "active.oceantri").find("mod test-pack 1") != std::string::npos, "activation persisted");

        std::vector<uint8_t> original(128, 0xAB);
        const uint64_t fileOffset = 32;
        ModManager::DiscView disc;
        disc.fileSize = [&](const std::string& name) { return name == "15/test.brres" ? uint32_t(brres.size()) : 0; };
        disc.fileOffset = [&](const std::string& name) { return name == "15/test.brres" ? fileOffset : 0; };
        const auto cacheRoot = ModManager::PrepareDiscCache(disc, root / "game");
        Check(cacheRoot != root / "game" && ModManager::HasAppliedFiles(), "file overlay gets separate cache namespace");
        Check(cacheRoot.string().find("mod-cache") != std::string::npos, "hash-named mod cache");
        std::vector<uint8_t> output = original;
        ModManager::OverlayRead(0, output.data(), output.size());
        Check(std::memcmp(output.data() + fileOffset, brres.data(), brres.size()) == 0, "exact replacement bytes overlaid");
        Check(output[0] == 0xAB && output[fileOffset + brres.size()] == 0xAB, "surrounding original bytes preserved");
        output = original;
        ModManager::OverlayRead(fileOffset + 10, output.data(), 20);
        Check(std::memcmp(output.data(), brres.data() + 10, 20) == 0, "partial DVD read overlay");
        Check(Read(packageDir / "files" / "test.brres") == brres, "package file untouched");

        // A changed replacement must not reuse the previous cache identity.
        auto changed = brres;
        changed[20] = 1;
        Write(packageDir / "files" / "test.brres", changed);
        ModManager::QueueRefresh();
        ModManager::Pump();
        view = ModManager::GetView();
        Check(!view.packages[0].valid && view.packages[0].error.find("SHA-256") != std::string::npos, "changed package rejected by integrity check");

        // Wrong original length is refused; no partial overlay is installed.
        Write(packageDir / "files" / "test.brres", brres);
        ModManager::QueueRefresh();
        ModManager::Pump();
        Check(ModManager::GetView().packages[0].valid, "restored package valid");
        ModManager::QueueToggle("test-pack", true);
        ModManager::Pump();
        disc.fileSize = [](const std::string&) { return 63; };
        const auto refused = ModManager::PrepareDiscCache(disc, root / "game");
        Check(refused == root / "game" && !ModManager::HasAppliedFiles(), "length mismatch keeps original assets/cache");
        disc.nativeTextureMap = true;
        disc.fileSize = [&](const std::string&) { return uint32_t(brres.size()); };
        const auto prepared = ModManager::PrepareDiscCache(disc, root / "game");
        Check(prepared == root / "game" && !ModManager::HasAppliedFiles(), "prepared native assets are not silently bypassed");

        ModManager::ResetForTests();
        std::filesystem::remove_all(root);
        std::cout << "Mod manager tests passed (manifest safety, SHA-256, activation, texture roots, overlay, cache isolation).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
