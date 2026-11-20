#include "gecko/gecko_manager.h"

#include "gecko/gecko_memory_guest.h"
#include "gecko/gecko_storage.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <utility>

namespace Gecko {

Manager::Manager(Memory& memory, const Limits& limits) : m_memory(memory), m_limits(limits), m_storagePath(CodesPath()) {}

void Manager::AddCode(Code code) {
    auto shared = std::make_shared<const Code>(std::move(code));

    std::lock_guard lock(m_mutex);
    m_pending.push_back({CommandKind::Add, shared->id, std::move(shared)});
    m_hasWork.store(true, std::memory_order_relaxed);
}

void Manager::Enable(uint64_t id, bool enabled) {
    std::lock_guard lock(m_mutex);
    m_pending.push_back({enabled ? CommandKind::Enable : CommandKind::Disable, id, {}});
    m_hasWork.store(true, std::memory_order_relaxed);
}

void Manager::Remove(uint64_t id) {
    std::lock_guard lock(m_mutex);
    m_pending.push_back({CommandKind::Remove, id, {}});
    m_hasWork.store(true, std::memory_order_relaxed);
}

void Manager::Reset(uint64_t id) {
    std::lock_guard lock(m_mutex);
    m_pending.push_back({CommandKind::Reset, id, {}});
    m_hasWork.store(true, std::memory_order_relaxed);
}

void Manager::Clear() {
    std::lock_guard lock(m_mutex);
    m_pending.push_back({CommandKind::Clear, 0, {}});
    m_hasWork.store(true, std::memory_order_relaxed);
}

void Manager::SetAutoDisableAfterErrors(uint32_t limit) {
    std::lock_guard lock(m_mutex);
    m_autoDisableAfterErrors = limit == 0 ? 1 : limit;
}

Manager::Entry* Manager::FindEntry(uint64_t id) {
    for (Entry& entry : m_entries)
        if (entry.code && entry.code->id == id)
            return &entry;

    return nullptr;
}

bool Manager::ApplyCommands(std::vector<Command> commands) {
    std::lock_guard lock(m_mutex);

    bool storageChanged = false;

    for (Command& command : commands) {
        switch (command.kind) {
        case CommandKind::Add: {
            if (!command.code)
                break;

            if (Entry* existing = FindEntry(command.id)) {
                existing->code = std::move(command.code);
                existing->state.Reset(*existing->code);
                existing->consecutiveErrors = 0;
                existing->diagnostics.clear();
            } else {
                Entry entry;
                entry.code = std::move(command.code);
                entry.enabled = false;
                entry.state.Reset(*entry.code);
                m_entries.push_back(std::move(entry));
            }

            storageChanged = true;
            break;
        }
        case CommandKind::Remove: {
            const auto before = m_entries.size();
            m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
                return entry.code && entry.code->id == command.id;
            }), m_entries.end());
            storageChanged = storageChanged || m_entries.size() != before;
            break;
        }
        case CommandKind::Enable:
        case CommandKind::Disable: {
            Entry* entry = FindEntry(command.id);

            if (!entry)
                break;

            const bool enabled = command.kind == CommandKind::Enable;

            if (entry->enabled != enabled) {
                entry->enabled = enabled;
                entry->state.Reset(*entry->code);
                entry->consecutiveErrors = 0;
                entry->diagnostics.clear();
            }

            break;
        }
        case CommandKind::Reset: {
            Entry* entry = FindEntry(command.id);

            if (entry) {
                entry->state.Reset(*entry->code);
                entry->consecutiveErrors = 0;
                entry->diagnostics.clear();
            }

            break;
        }
        case CommandKind::Clear:
            if (!m_entries.empty()) {
                m_entries.clear();
                storageChanged = true;
            }
            break;
        }
    }

    return storageChanged;
}

void Manager::Step() {
    if (!m_hasWork.load(std::memory_order_relaxed))
        return;

    std::vector<Command> commands;

    {
        std::lock_guard lock(m_mutex);

        if (m_pending.empty() && m_entries.empty()) {
            m_hasWork.store(false, std::memory_order_relaxed);
            return;
        }

        commands.swap(m_pending);
    }

    bool storageChanged = false;

    if (!commands.empty())
        storageChanged = ApplyCommands(std::move(commands));

    if (storageChanged)
        SaveToStorage();

    std::vector<Entry*> enabled;

    {
        std::lock_guard lock(m_mutex);

        for (Entry& entry : m_entries)
            if (entry.enabled)
                enabled.push_back(&entry);

        m_hasWork.store(!m_pending.empty() || !enabled.empty(), std::memory_order_relaxed);
    }

    if (enabled.empty())
        return;

    uint32_t instructions = 0;
    uint32_t accesses = 0;

    for (Entry* entry : enabled) {
        if (!entry->code || !entry->code->valid || entry->code->support == SupportLevel::Unsupported) {
            std::lock_guard lock(m_mutex);

            if (entry->enabled) {
                entry->enabled = false;
                entry->state.Reset(*entry->code);

                Diagnostic diagnostic;
                diagnostic.severity = DiagnosticSeverity::Warning;
                diagnostic.codeName = entry->code->name;
                diagnostic.message = "code is not executable and was disabled";
                entry->diagnostics.push_back(std::move(diagnostic));
            }

            continue;
        }

        entry->state.BeginFrame();

        const ExecutionResult result = Interpreter::Run(*entry->code, m_memory, entry->state, m_limits);
        instructions += uint32_t(result.instructionsExecuted);
        accesses += uint32_t(result.memoryAccesses);

        if (result.ok()) {
            entry->consecutiveErrors = 0;
            continue;
        }

        std::lock_guard lock(m_mutex);

        ++entry->consecutiveErrors;

        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.line = int(result.error.instructionIndex);
        diagnostic.codeName = entry->code->name;
        diagnostic.message = std::string(ExecutionStatusName(result.status)) + ": " + result.error.message;
        entry->diagnostics.push_back(std::move(diagnostic));

        if (entry->diagnostics.size() > 8)
            entry->diagnostics.erase(entry->diagnostics.begin());

        if (entry->consecutiveErrors >= m_autoDisableAfterErrors) {
            entry->enabled = false;
            entry->state.Reset(*entry->code);
            entry->consecutiveErrors = 0;

            Diagnostic disabled;
            disabled.severity = DiagnosticSeverity::Warning;
            disabled.codeName = entry->code->name;
            disabled.message = "disabled after repeated execution errors";
            entry->diagnostics.push_back(std::move(disabled));
        }
    }

    std::lock_guard lock(m_mutex);
    m_totalInstructionsExecuted += instructions;
    m_totalMemoryAccesses += accesses;
}

ManagerSnapshot Manager::GetSnapshot() const {
    ManagerSnapshot snapshot;

    std::lock_guard lock(m_mutex);

    snapshot.totalInstructionsExecuted = m_totalInstructionsExecuted;
    snapshot.totalMemoryAccesses = m_totalMemoryAccesses;
    snapshot.storageError = m_storageError;

    for (const Entry& entry : m_entries) {
        if (!entry.code)
            continue;

        CodeSnapshot code;
        code.id = entry.code->id;
        code.name = entry.code->name;
        code.description = entry.code->description;
        code.sourceText = entry.code->sourceText;
        code.support = entry.code->support;
        code.gameCompatibility = entry.code->gameCompatibility;
        code.enabled = entry.enabled;
        code.valid = entry.code->valid;
        code.parseDiagnostics = entry.code->diagnostics;
        code.runtimeDiagnostics = entry.diagnostics;

        if (entry.enabled)
            ++snapshot.enabledCount;

        snapshot.codes.push_back(std::move(code));
    }

    return snapshot;
}

uint32_t Manager::EnabledCount() const {
    std::lock_guard lock(m_mutex);

    // Account for commands that were posted but not applied yet, so the
    // online gate cannot observe a stale "all disabled" state between an
    // enable request and the next logic step.
    std::unordered_map<uint64_t, bool> state;

    for (const Entry& entry : m_entries)
        if (entry.code)
            state[entry.code->id] = entry.enabled;

    for (const Command& command : m_pending) {
        switch (command.kind) {
        case CommandKind::Add:
            if (command.code)
                state.emplace(command.code->id, false);
            break;
        case CommandKind::Remove:
            state.erase(command.id);
            break;
        case CommandKind::Enable:
            if (auto it = state.find(command.id); it != state.end())
                it->second = true;
            break;
        case CommandKind::Disable:
            if (auto it = state.find(command.id); it != state.end())
                it->second = false;
            break;
        case CommandKind::Clear:
            state.clear();
            break;
        case CommandKind::Reset:
            break;
        }
    }

    uint32_t count = 0;

    for (const auto& [id, enabled] : state)
        count += enabled ? 1u : 0u;

    return count;
}

void Manager::LoadInitialCodes() {
    std::lock_guard lock(m_mutex);

    if (m_storageLoaded)
        return;

    m_storageLoaded = true;

    std::vector<Code> codes;
    std::vector<Diagnostic> diagnostics;

    if (!LoadCodes(m_storagePath, codes, diagnostics)) {
        m_storageError = "failed to load " + m_storagePath;
        std::fprintf(stderr, "MH3 gecko: %s\n", m_storageError.c_str());
    } else {
        m_storageError.clear();
    }

    for (const Diagnostic& diagnostic : diagnostics)
        std::fprintf(stderr, "MH3 gecko: %s\n", diagnostic.message.c_str());

    for (Code& code : codes) {
        if (FindEntry(code.id))
            continue;

        Entry entry;
        entry.code = std::make_shared<const Code>(std::move(code));
        entry.enabled = false;
        entry.state.Reset(*entry.code);
        m_entries.push_back(std::move(entry));
    }

    m_hasWork.store(!m_entries.empty(), std::memory_order_relaxed);
}

void Manager::SetStoragePath(std::string path) {
    std::lock_guard lock(m_mutex);
    m_storagePath = std::move(path);
    m_storageLoaded = false;
}

void Manager::SaveToStorage() {
    std::vector<Code> codes;

    {
        std::lock_guard lock(m_mutex);
        codes.reserve(m_entries.size());

        for (const Entry& entry : m_entries)
            if (entry.code)
                codes.push_back(*entry.code);
    }

    const bool saved = SaveCodes(m_storagePath, codes);

    {
        std::lock_guard lock(m_mutex);
        m_storageError = saved ? std::string() : ("failed to save " + m_storagePath);
    }

    if (!saved)
        std::fprintf(stderr, "MH3 gecko: failed to save %s\n", m_storagePath.c_str());
}

Manager& Runtime() {
    static GuestMemory memory;
    static Manager manager(memory);
    return manager;
}

} // namespace Gecko
