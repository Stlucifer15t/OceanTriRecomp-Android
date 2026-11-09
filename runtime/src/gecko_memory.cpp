// The guest RAM the Gecko engine acts on (gecko_engine.h).
//
// Every access goes through the runtime's own memory layer rather than a raw
// host pointer, so a code that names an unmapped address fails instead of
// taking the app down; and a write into the loaded, translated image is refused
// - the same rule the game's own ports follow (executable_guard.cpp): that
// memory is not what the ARM64 code executes from, so patching it would change
// nothing but the image checks.
#include "gecko_engine.h"

#include "guest_flat_memory.h"
#include "memory.h"
#include "recomp_mod_loader.h"

#include <cstring>
#include <exception>

namespace Gecko {
namespace {

class GuestRam final : public Memory {
  public:
    bool Read(uint32_t address, void* out, size_t bytes) override {
        if (!InRam(address, bytes))
            return false;

        std::memcpy(out, GuestFlat::HostPointer(address, bytes), bytes);
        return true;
    }

    bool Write(uint32_t address, const void* bytes, size_t count) override {
        if (!Writable(address, count))
            return false;

        std::memcpy(GuestFlat::HostPointer(address, count), bytes, count);
        return true;
    }

    bool InRam(uint32_t address, size_t bytes) override {
        return bytes != 0 && GuestFlat::HostPointer(address, bytes) != nullptr;
    }

    bool Writable(uint32_t address, size_t bytes) override {
        if (!InRam(address, bytes))
            return false;

        // CheckExecutableWrite lets a store through when it writes the bytes
        // already there, so asking it with the current contents answers "may
        // this range be changed at all" without needing the new value.
        uint64_t current = 0;
        const size_t read = bytes < sizeof(current) ? bytes : sizeof(current);
        std::memcpy(&current, GuestFlat::HostPointer(address, bytes), read);

        try {
            RecompMod::CheckExecutableWrite(address, bytes, current);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
};

} // namespace

Memory& GuestMemory() {
    static GuestRam ram;
    return ram;
}

} // namespace Gecko
