#pragma once

#include "gecko/gecko_memory.h"

namespace Gecko {

// The production guest-memory backend: it connects the interpreter to
// Ocean's Memory API. Every access is range-checked against MEM1/MEM2 and
// alignment-checked before it reaches the underlying, unchecked host access
// layer.
class GuestMemory final : public Memory {
  public:
    bool Read8(uint32_t address, uint8_t& value) const override;
    bool Read16(uint32_t address, uint16_t& value) const override;
    bool Read32(uint32_t address, uint32_t& value) const override;
    bool Write8(uint32_t address, uint8_t value) override;
    bool Write16(uint32_t address, uint16_t value) override;
    bool Write32(uint32_t address, uint32_t value) override;
    bool Valid(uint32_t address, uint32_t size) const override;

  private:
    static bool InGuestRam(uint32_t address, uint32_t size);
    static bool Aligned(uint32_t address, uint32_t size);
};

} // namespace Gecko
