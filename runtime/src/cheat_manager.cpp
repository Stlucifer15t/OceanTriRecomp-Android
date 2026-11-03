#include "cheat_manager.h"

#include "cheats.h"
#include "custom_codes.h"
#include "fs_status.h"
#include "launcher_cheats.h"
#include "memory.h"
#include "net/local_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace CheatManager {
namespace {

struct Command {
    Action action;
    size_t native = 0;
    uint32_t value = 0;
    std::filesystem::path path;
};

std::mutex g_queueMutex;
std::mutex g_statusMutex;
std::vector<Command> g_queue;
std::string g_status = "Ready. Offline use only.";
std::filesystem::path g_root;
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_master{false};
std::atomic<uint64_t> g_revision{0};
std::array<std::atomic<uint8_t>, kMaxNativeCheats> g_enabled{};
std::array<std::atomic<uint32_t>, kMaxNativeCheats> g_selected{};

struct Curated {
    Cheats::Id id;
    Category category;
    const char* title;
    const char* detail;
    bool saveRisk;
    bool oneShot;
};

constexpr Curated kCurated[]{
    {Cheats::Id::InfiniteHealth, Category::Player, "Infinite health", "Keeps health and recoverable health at your current maximum.", false, false},
    {Cheats::Id::InfiniteStamina, Category::Player, "Infinite stamina", "Keeps stamina at your current maximum.", false, false},
    {Cheats::Id::InfiniteAir, Category::Player, "Infinite oxygen", "Keeps the oxygen gauge full while swimming.", false, false},
    {Cheats::Id::WeaponsSharp, Category::Player, "Infinite sharpness", "Refills the weapon's own sharpness cap; does not change its color tier.", false, false},
    {Cheats::Id::UnlimitedConsumables, Category::Items, "Unlimited consumables", "Local item-use calls do not subtract pouch items.", false, false},
    {Cheats::Id::InfiniteAmmo, Category::Items, "Unlimited ammunition", "Bowgun shots do not consume pouch ammunition.", false, false},
    {Cheats::Id::BoxItemsX99, Category::Items, "Item box quantities x99", "Nonempty box stacks refill when the box reads them. Can save changed quantities.", true, false},
    {Cheats::Id::OneHitKills, Category::Combat, "One-hit kills", "A damaging hit empties a monster's health, including its capture-phase floor.", false, false},
    {Cheats::Id::MaximumDamage, Category::Combat, "Maximum damage", "Weapon attack reads as 32767 while enabled. Reopen equipment stats after changing.", false, false},
    {Cheats::Id::WeaponsSharp, Category::Combat, "No sharpness loss", "Shares the Player sharpness switch; preserves the weapon's own cap.", false, false},
    {Cheats::Id::MaxMoney, Category::Progression, "Maximum money", "Holds money at 9,999,999z. Saving keeps the changed amount.", true, false},
    {Cheats::Id::AllEquipment, Category::Progression, "Unlock weapons and armor", "One-shot: fills free equipment-box slots when enabled. Back up first; items remain after OFF.", true, true},
    {Cheats::Id::AllStarQuests, Category::Progression, "Unlock quest tiers", "Unlocks star-quest flags when read. These ordinary save flags can persist.", true, false},
    {Cheats::Id::AllItems, Category::Progression, "Fill item box", "One-shot: replaces the item box with storable items. Back up first; items remain after OFF.", true, true},
};

bool OnlineSessionActive() noexcept {
    if (LocalOnlineServer::IsLocalServerRunning())
        return true;
    return Memory::Contains(0x8065AA84u, 1) && Memory::Read8(0x8065AA84u) == 2;
}

bool ParseNumber(std::string_view text, uint32_t& value) noexcept {
    if (text.empty())
        return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

void SetStatus(std::string text) noexcept {
    try {
        std::lock_guard lock(g_statusMutex);
        g_status = std::move(text);
    } catch (...) {
        // Allocation failure must not turn a UI diagnostic into a game crash.
    }
}

void MirrorState() noexcept {
    const size_t count = std::min(LauncherCheats::List().size(), kMaxNativeCheats);
    for (size_t i = 0; i < count; ++i) {
        g_enabled[i].store(LauncherCheats::Enabled(i) ? 1 : 0, std::memory_order_relaxed);
        g_selected[i].store(uint32_t(LauncherCheats::Selected(i)), std::memory_order_relaxed);
    }
    g_master.store(Cheats::g_active.load(std::memory_order_relaxed), std::memory_order_relaxed);
    g_revision.fetch_add(1, std::memory_order_release);
}

Result ApplyConfiguration(const Configuration& config) noexcept {
    // Validate the whole imported profile before changing either subsystem.
    const auto custom = CustomCodes::GetView();
    for (const auto id : config.customEnabled) {
        bool found = false;
        for (const auto& code : custom.codes)
            found |= code.id == id;
        if (!found)
            return {false, "Profile references a custom code that is not imported (or whose reviewed mapping changed)."};
    }
    CustomCodes::ApplyEnabled(config.customEnabled);
    Cheats::SetActive(false);
    for (size_t i = 0; i < config.count; ++i) {
        LauncherCheats::Select(i, config.selected[i]);
        LauncherCheats::Enable(i, config.enabled[i] != 0);
    }
    Cheats::SetActive(config.master);
    MirrorState();
    return {};
}

Result WriteFile(const std::filesystem::path& path, std::string_view text) noexcept {
    try {
        if (!path.parent_path().empty()) {
            const auto status = FsStatus::CreateDirectories(path.parent_path());
            if (!status.ok())
                return {false, status.message()};
        }
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(text.data(), std::streamsize(text.size()));
            out.close();
            if (!out)
                return {false, "Cannot write cheat configuration (storage full or read-only)."};
        }
        const auto status = FsStatus::Rename(temporary, path);
        return {status.ok(), status.message()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cheat configuration filesystem failure."};
    }
}

Result ReadFile(const std::filesystem::path& path, std::string& text) noexcept {
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {false, "Cannot open " + path.filename().string()};
        const auto size = in.tellg();
        if (size < 0 || size > 65536)
            return {false, "Cheat configuration is empty, unreadable or larger than 64 KiB."};
        text.resize(size_t(size));
        in.seekg(0);
        if (!text.empty())
            in.read(text.data(), std::streamsize(text.size()));
        return in ? Result{} : Result{false, "Short read while importing cheat configuration."};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cheat configuration filesystem failure."};
    }
}

} // namespace

std::span<const Entry> Catalog() noexcept {
    static const std::vector<Entry> entries = [] {
        std::vector<Entry> result;
        std::array<bool, kMaxNativeCheats> curated{};
        const auto native = LauncherCheats::List();
        for (const auto& item : kCurated) {
            const size_t index = size_t(item.id);
            curated[index] = true;
            result.push_back({index, item.category, item.title, item.detail, item.saveRisk, item.oneShot, false});
        }
        for (size_t i = 0; i < native.size() && i < kMaxNativeCheats; ++i) {
            // Typed-name cheats remain CLI-only: a touch toggle without its
            // required text would be a nonfunctional switch.
            if (!curated[i] && native[i].textLength == 0)
                result.push_back({i, Category::Advanced, native[i].name, native[i].detail, true, false, true});
        }
        return result;
    }();
    return entries;
}

const char* CategoryName(Category category) noexcept {
    switch (category) {
    case Category::Player: return "Player";
    case Category::Items: return "Items";
    case Category::Combat: return "Combat";
    case Category::Progression: return "Progression";
    case Category::Advanced: return "Advanced";
    }
    return "Other";
}

Result DecodeConfig(std::string_view text, Configuration& out) noexcept {
    try {
        if (text.empty() || text.size() > 65536)
            return {false, "Configuration must be 1..65536 bytes."};
        const size_t count = LauncherCheats::List().size();
        if (count > kMaxNativeCheats)
            return {false, "Native cheat table exceeds the configuration capacity."};
        Configuration parsed;
        parsed.count = count;
        for (size_t i = 0; i < count; ++i)
            parsed.selected[i] = uint32_t(LauncherCheats::Selected(i));
        std::array<bool, kMaxNativeCheats> seen{};
        std::istringstream in{std::string(text)};
        std::string line;
        bool header = false, game = false, master = false;
        unsigned lines = 0;
        while (std::getline(in, line)) {
            if (++lines > 2048)
                return {false, "Too many configuration lines."};
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream row(line);
            std::string tag;
            row >> tag;
            if (tag == "OCEANTRI-CHEATS") {
                uint32_t version;
                std::string versionText, extra;
                if (header || !(row >> versionText) || !ParseNumber(versionText, version) || version != 1 || (row >> extra))
                    return {false, "Invalid or duplicate configuration header."};
                header = true;
            } else if (tag == "game") {
                std::string id, extra;
                if (game || !(row >> id) || id != "RMHP08" || (row >> extra))
                    return {false, "Configuration is not for PAL RMHP08."};
                game = true;
            } else if (tag == "master") {
                uint32_t value;
                std::string valueText, extra;
                if (master || !(row >> valueText) || !ParseNumber(valueText, value) || value > 1 || (row >> extra))
                    return {false, "Invalid or duplicate master switch."};
                parsed.master = value != 0;
                master = true;
            } else if (tag == "native") {
                uint32_t index, enabled, selected;
                std::string a, b, c, extra;
                if (!(row >> a >> b >> c) || !ParseNumber(a, index) || !ParseNumber(b, enabled) ||
                    !ParseNumber(c, selected) || (row >> extra) || index >= count || seen[index] || enabled > 1 ||
                    (LauncherCheats::Choices(index) != 0 && selected >= LauncherCheats::Choices(index)) ||
                    (LauncherCheats::Choices(index) == 0 && selected != 0))
                    return {false, "Invalid, duplicate or out-of-range native cheat record."};
                seen[index] = true;
                parsed.enabled[index] = uint8_t(enabled);
                parsed.selected[index] = selected;
            } else if (tag == "custom") {
                std::string idText, enabledText, extra;
                uint64_t id;
                uint32_t enabled;
                if (!(row >> idText >> enabledText) || (row >> extra) || idText.empty() || idText.size() > 16 || !ParseNumber(enabledText, enabled) || enabled != 1)
                    return {false, "Expected custom <hex id> 1."};
                const auto [end, error] = std::from_chars(idText.data(), idText.data() + idText.size(), id, 16);
                if (error != std::errc{} || end != idText.data() + idText.size() || id == 0 || parsed.customEnabled.size() >= CustomCodes::kMaxCodes ||
                    std::find(parsed.customEnabled.begin(), parsed.customEnabled.end(), id) != parsed.customEnabled.end())
                    return {false, "Invalid or duplicate custom-code identity."};
                parsed.customEnabled.push_back(id);
            } else {
                return {false, "Unknown configuration field: " + tag.substr(0, 40)};
            }
        }
        if (!header || !game || !master)
            return {false, "Missing configuration header, game ID or master switch."};
        out = std::move(parsed);
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Invalid cheat configuration."};
    }
}

std::string EncodeConfig() noexcept {
    std::ostringstream out;
    out << "OCEANTRI-CHEATS 1\ngame RMHP08\nmaster " << (Cheats::g_active.load(std::memory_order_relaxed) ? 1 : 0) << '\n';
    const size_t count = std::min(LauncherCheats::List().size(), kMaxNativeCheats);
    for (size_t i = 0; i < count; ++i) {
        if (LauncherCheats::Enabled(i) || LauncherCheats::Choices(i) != 0)
            out << "native " << i << ' ' << unsigned(LauncherCheats::Enabled(i)) << ' ' << LauncherCheats::Selected(i) << '\n';
    }
    for (const auto id : CustomCodes::EnabledIds())
        out << "custom " << std::hex << id << std::dec << " 1\n";
    return out.str();
}

Result ExportConfig(const std::filesystem::path& path) noexcept {
    return WriteFile(path, EncodeConfig());
}

Result ImportConfig(const std::filesystem::path& path) noexcept {
    std::string text;
    auto result = ReadFile(path, text);
    if (!result.ok)
        return result;
    Configuration config;
    result = DecodeConfig(text, config);
    if (!result.ok)
        return result;
    // Imported profiles never activate themselves. Review, then enable.
    config.master = false;
    result = ApplyConfiguration(config);
    if (result.ok)
        result.message = "Imported with master OFF. Review switches before enabling.";
    return result;
}

Result Initialize(const std::filesystem::path& userDirectory) noexcept {
    try {
        if (LauncherCheats::List().size() > kMaxNativeCheats)
            return {false, "Native cheat table is too large."};
        g_root = userDirectory;
        CustomCodes::Initialize(g_root);
        const bool cliUsed = Cheats::g_used.load(std::memory_order_relaxed);
        std::array<uint8_t, kMaxNativeCheats> cliEnabled{};
        std::array<uint32_t, kMaxNativeCheats> cliSelected{};
        for (size_t i = 0; i < LauncherCheats::List().size(); ++i) {
            cliEnabled[i] = LauncherCheats::Enabled(i);
            cliSelected[i] = uint32_t(LauncherCheats::Selected(i));
        }
        Configuration config;
        config.count = LauncherCheats::List().size();
        std::string warning;
        const auto path = g_root / "cheats" / "active.oceantri";
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            std::string text;
            auto result = ReadFile(path, text);
            if (result.ok)
                result = DecodeConfig(text, config);
            if (!result.ok) {
                // A corrupt user profile must not prevent the game from booting.
                config = Configuration{};
                config.count = LauncherCheats::List().size();
                warning = "Saved cheat profile rejected (" + result.message + "); starting with cheats OFF.";
            }
        }
        auto result = ApplyConfiguration(config);
        if (!result.ok)
            return result;
        if (cliUsed) {
            for (size_t i = 0; i < config.count; ++i) {
                if (cliEnabled[i]) {
                    LauncherCheats::Select(i, cliSelected[i]);
                    LauncherCheats::Enable(i, true);
                }
            }
            Cheats::SetActive(true);
        }
        MirrorState();
        g_initialized.store(true, std::memory_order_release);
        if (!warning.empty())
            SetStatus(std::move(warning));
        else
            SetStatus(cliUsed ? "CLI cheats active. Profile merging preserves CLI selections." : "Ready. Back up saves before using progression cheats.");
        return {};
    } catch (const std::exception& e) {
        return {false, e.what()};
    } catch (...) {
        return {false, "Cannot initialize cheat manager."};
    }
}

bool Queue(Action action, size_t native, uint32_t value) noexcept {
    try {
        if (action == Action::Import || action == Action::Export)
            return false;
        if ((action == Action::Toggle || action == Action::Select) && native >= LauncherCheats::List().size())
            return false;
        std::lock_guard lock(g_queueMutex);
        if (g_queue.size() >= 64)
            return false;
        g_queue.push_back({action, native, value, {}});
        return true;
    } catch (...) {
        return false;
    }
}

bool QueueFile(Action action, const std::filesystem::path& path) noexcept {
    try {
        if ((action != Action::Import && action != Action::Export) || path.empty())
            return false;
        std::lock_guard lock(g_queueMutex);
        if (g_queue.size() >= 64)
            return false;
        g_queue.push_back({action, 0, 0, path});
        return true;
    } catch (...) {
        return false;
    }
}

void Pump() noexcept {
    if (!g_initialized.load(std::memory_order_acquire))
        return;
    std::vector<Command> commands;
    {
        std::lock_guard lock(g_queueMutex);
        commands.swap(g_queue);
    }
    // Custom toggles have their own bounded queue and must drain even when no
    // native manager command was issued this frame.
    CustomCodes::Pump();
    if (commands.empty())
        return;
    bool dirty = false;
    std::string message;
    for (const auto& command : commands) {
        bool changed = false;
        switch (command.action) {
        case Action::Master:
            if (command.value && OnlineSessionActive()) {
                message = "Cheats cannot be enabled during an online session. Return offline and relaunch for a clean guard.";
                break;
            }
            Cheats::SetActive(command.value != 0);
            message = command.value ? "Cheats enabled for offline play." : "Master OFF. Existing save/stat changes are not reverted.";
            changed = dirty = true;
            break;
        case Action::Toggle:
            if (command.value && OnlineSessionActive()) {
                message = "Cheats cannot be enabled during an online session.";
                break;
            }
            if (command.native < LauncherCheats::List().size()) {
                LauncherCheats::Enable(command.native, command.value != 0);
                changed = dirty = true;
            }
            break;
        case Action::Select:
            if (command.native < LauncherCheats::List().size() && command.value < LauncherCheats::Choices(command.native)) {
                LauncherCheats::Select(command.native, command.value);
                changed = dirty = true;
            }
            break;
        case Action::DisableAll:
            Cheats::SetActive(false);
            CustomCodes::ApplyEnabled({});
            for (size_t i = 0; i < LauncherCheats::List().size(); ++i)
                LauncherCheats::Enable(i, false);
            message = "All switches OFF. Save changes remain; online play needs a fresh launch.";
            changed = dirty = true;
            break;
        case Action::RepeatUnlocks:
            if (Cheats::On(Cheats::Id::AllItems))
                Cheats::RequestBoxFill(false);
            if (Cheats::On(Cheats::Id::AllEquipment))
                Cheats::RequestBoxFill(true);
            message = "Enabled box unlocks queued for the next safe game step.";
            break;
        case Action::Import: {
            const auto result = ImportConfig(command.path);
            message = result.message;
            changed = dirty = result.ok;
            break;
        }
        case Action::Export: {
            const auto result = ExportConfig(command.path);
            message = result.ok ? "Exported: " + command.path.filename().string() : result.message;
            break;
        }
        case Action::Save:
            dirty = true;
            message = "Cheat profile saved separately from Wii saves.";
            break;
        case Action::RefreshCustom:
            CustomCodes::Refresh();
            dirty = true;
            message = CustomCodes::GetView().status;
            break;
        case Action::ResetLatches:
            Cheats::ResetLatches();
            message = "Button-activated cheat latches reset; save changes are not reverted.";
            break;
        }
        if (changed)
            MirrorState();
    }
    if (dirty && g_root.empty())
        dirty = false;
    if (dirty) {
        const auto result = ExportConfig(g_root / "cheats" / "active.oceantri");
        if (!result.ok)
            message = result.message;
    }
    CustomCodes::Pump();
    if (!message.empty())
        SetStatus(std::move(message));
}

Snapshot GetSnapshot() noexcept {
    Snapshot snapshot;
    snapshot.initialized = g_initialized.load(std::memory_order_acquire);
    snapshot.master = g_master.load(std::memory_order_relaxed);
    snapshot.used = Cheats::g_used.load(std::memory_order_relaxed);
    snapshot.count = std::min(LauncherCheats::List().size(), kMaxNativeCheats);
    snapshot.revision = g_revision.load(std::memory_order_acquire);
    for (size_t i = 0; i < snapshot.count; ++i) {
        snapshot.enabled[i] = g_enabled[i].load(std::memory_order_relaxed);
        snapshot.selected[i] = g_selected[i].load(std::memory_order_relaxed);
    }
    try {
        std::lock_guard lock(g_statusMutex);
        snapshot.status = g_status;
    } catch (...) {
        snapshot.status = "Cheat manager status unavailable.";
    }
    return snapshot;
}

std::filesystem::path ImportPath() noexcept { return g_root / "cheats" / "import.oceantri"; }
std::filesystem::path ExportPath() noexcept { return g_root / "cheats" / "export.oceantri"; }

void ResetForTests() noexcept {
    CustomCodes::ResetForTests();
    Cheats::SetActive(false);
    for (size_t i = 0; i < LauncherCheats::List().size(); ++i) {
        LauncherCheats::Enable(i, false);
        LauncherCheats::Select(i, 0);
    }
    Cheats::ResetLatches();
    Cheats::g_used.store(false, std::memory_order_relaxed);
    Cheats::SetActive(true); // pre-manager CLI/launcher default, with no switches selected
    g_initialized.store(false, std::memory_order_relaxed);
    g_root.clear();
    {
        std::lock_guard lock(g_queueMutex);
        g_queue.clear();
    }
    MirrorState();
    SetStatus("Ready. Offline use only.");
}

} // namespace CheatManager
