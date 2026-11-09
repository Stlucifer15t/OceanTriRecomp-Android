#pragma once

// Executing Gecko codes against a memory interface.
//
// The data code types - writes, ifs, base and pointer arithmetic - run exactly
// as Gecko's code handler runs them (docs/codehandler.s of the Gecko OS
// handler, which the community's lists are written against), once per logic
// step where the handler ran them once per frame.
//
// The operations that execute PowerPC machine code (C0/C2/C6) cannot work in a
// static recompilation: the ARM64 functions were produced at build time and
// there is no interpreter in the game's path. They are refused with a reason,
// and the same cheat exists as a native port (runtime/src/cheats.cpp).
//
// Everything is written against `Memory`, so the host tests drive the same code
// with a fake memory and the game drives it with the guest's RAM.
#include "gecko_codes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Gecko {

// The memory a code may use. Addresses are byte-addressed guest addresses.
// `Writable` is false for the loaded executable image, so a code that would
// patch translated code is refused instead of corrupting it.
class Memory {
  public:
    virtual ~Memory() = default;

    virtual bool Read(uint32_t address, void* out, size_t count) = 0;
    virtual bool Write(uint32_t address, const void* bytes, size_t count) = 0;

    // The address is backed by guest RAM (MEM1, MEM2 or their aliases).
    virtual bool InRam(uint32_t address, size_t count) = 0;

    // A write there may not change translated code or the loaded tables.
    virtual bool Writable(uint32_t address, size_t count) = 0;

    bool Read8(uint32_t address, uint8_t& value) { return Read(address, &value, 1); }
    bool Read16(uint32_t address, uint16_t& value);
    bool Read32(uint32_t address, uint32_t& value);
    bool Write8(uint32_t address, uint8_t value) { return Write(address, &value, 1); }
    bool Write16(uint32_t address, uint16_t value);
    bool Write32(uint32_t address, uint32_t value);
};

// The guest's RAM, as the game sees it.
Memory& GuestMemory();

struct Result {
    bool ok = true;
    // How many stores reached guest memory this step.
    size_t writes = 0;
    std::string error;

    static Result Failure(std::string message) {
        Result result;
        result.ok = false;
        result.error = std::move(message);
        return result;
    }
};

struct Options {
    // The buttons held this step, in this runtime's Classic Controller bits
    // (cheats.h Button): an activator line compares them.
    uint16_t held = 0;

    // Runs the code even when its activator line's buttons are not held (the
    // manager's "run once").
    bool ignoreActivator = false;
};

// Runs one code for one logic step.
Result Run(const Code& code, Memory& memory, const Options& options = {});

// Runs `code` once, ignoring its activator.
Result RunOnce(const Code& code, Memory& memory);

// The code's per-step plan: its lines with the region's addresses translated,
// which is what `Run` executes.
std::vector<Line> Plan(const Code& code, Region source);

// ---------------------------------------------------------------------------
// The PowerPC interpreter for injected blocks (C0/C2/C6)

struct BlockResult {
    bool ok = true;
    std::string error;
    uint32_t registers[32]{}; // the interpreter's final register file
};

// Runs a block of PowerPC words. The registers start at zero.
BlockResult ExecuteBlock(const std::vector<uint32_t>& words, Memory& memory);

// Whether the interpreter can run a block: every register it reads must be one
// it wrote first (otherwise it would silently compute from zeros instead of the
// caller's values), and it must not leave itself. `reason` explains a refusal.
bool BlockRunnable(const std::vector<uint32_t>& words, std::string& reason);

} // namespace Gecko
