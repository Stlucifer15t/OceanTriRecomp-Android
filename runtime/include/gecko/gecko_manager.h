#pragma once

#include "gecko/gecko_code.h"
#include "gecko/gecko_interpreter.h"
#include "gecko/gecko_memory.h"
#include "gecko/gecko_storage.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Gecko {

struct CodeSnapshot {
    uint64_t id = 0;
    std::string name;
    std::string description;
    std::string sourceText;
    SupportLevel support = SupportLevel::Unknown;
    GameCompatibility gameCompatibility = GameCompatibility::Unknown;
    bool enabled = false;
    bool valid = false;
    std::vector<Diagnostic> parseDiagnostics;
    std::vector<Diagnostic> runtimeDiagnostics;
};

struct ManagerSnapshot {
    std::vector<CodeSnapshot> codes;
    uint32_t enabledCount = 0;
    uint32_t totalInstructionsExecuted = 0;
    uint32_t totalMemoryAccesses = 0;
    std::string storageError;
};

// The runtime-wide Gecko manager. All mutation (add, remove, enable, disable,
// reset) is posted as a command and applied on the guest execution thread;
// `Step()` is the only place that executes codes against live guest memory.
class Manager {
  public:
    explicit Manager(Memory& memory, const Limits& limits = {});

    void AddCode(Code code);
    void Enable(uint64_t id, bool enabled);
    void Remove(uint64_t id);
    void Reset(uint64_t id);
    void Clear();

    void Step();

    ManagerSnapshot GetSnapshot() const;
    uint32_t EnabledCount() const;

    // A code that fails this many times in a row is disabled automatically.
    void SetAutoDisableAfterErrors(uint32_t limit);

    // Persists the current collection to m_storagePath.
    void LoadInitialCodes();
    void SetStoragePath(std::string path);
    const std::string& StoragePath() const { return m_storagePath; }

  private:
    struct Entry {
        std::shared_ptr<const Code> code;
        bool enabled = false;
        State state;
        std::vector<Diagnostic> diagnostics;
        uint32_t consecutiveErrors = 0;
    };

    enum class CommandKind { Add, Remove, Enable, Disable, Reset, Clear };

    struct Command {
        CommandKind kind = CommandKind::Add;
        uint64_t id = 0;
        std::shared_ptr<const Code> code;
    };

    bool ApplyCommands(std::vector<Command> commands);
    void SaveToStorage();
    Entry* FindEntry(uint64_t id);

    Memory& m_memory;
    Limits m_limits;
    uint32_t m_autoDisableAfterErrors = 4;
    uint32_t m_totalInstructionsExecuted = 0;
    uint32_t m_totalMemoryAccesses = 0;

    mutable std::mutex m_mutex;
    std::atomic<bool> m_hasWork{false};
    std::vector<Entry> m_entries;
    std::vector<Command> m_pending;
    std::string m_storagePath;
    std::string m_storageError;
    bool m_storageLoaded = false;
};

// The runtime-wide manager, backed by Ocean's guest memory.
Manager& Runtime();

} // namespace Gecko
