#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Gecko {

// Abstract guest memory. The interpreter only talks to this interface, so it
// can be tested with a fake and later backed by Ocean's guest RAM (Phase 2).
// Every method validates its address and size and returns false instead of
// touching host memory when the access would be out of range or misaligned.
class Memory {
  public:
    virtual ~Memory() = default;

    virtual bool Read8(uint32_t address, uint8_t& value) const = 0;
    virtual bool Read16(uint32_t address, uint16_t& value) const = 0;
    virtual bool Read32(uint32_t address, uint32_t& value) const = 0;
    virtual bool Write8(uint32_t address, uint8_t value) = 0;
    virtual bool Write16(uint32_t address, uint16_t value) = 0;
    virtual bool Write32(uint32_t address, uint32_t value) = 0;

    // Whether the whole [address, address + size) range is valid. The size is
    // not checked for alignment here; the individual read/write methods are.
    virtual bool Valid(uint32_t address, uint32_t size) const = 0;
};

// A bounded, contiguous memory for automated tests and future host
// integration. It stores host byte order and never dereferences an address
// outside its own buffer.
class FlatMemory final : public Memory {
  public:
    FlatMemory(uint32_t base, size_t size, std::vector<uint8_t> bytes = {});

    bool Read8(uint32_t address, uint8_t& value) const override;
    bool Read16(uint32_t address, uint16_t& value) const override;
    bool Read32(uint32_t address, uint32_t& value) const override;
    bool Write8(uint32_t address, uint8_t value) override;
    bool Write16(uint32_t address, uint16_t value) override;
    bool Write32(uint32_t address, uint32_t value) override;
    bool Valid(uint32_t address, uint32_t size) const override;

    uint8_t* Data() { return m_bytes.data(); }
    const uint8_t* Data() const { return m_bytes.data(); }
    size_t Size() const { return m_bytes.size(); }
    uint32_t Base() const { return m_base; }
    uint32_t End() const { return m_base + uint32_t(m_bytes.size()); }

  private:
    bool InRange(uint32_t address, uint32_t size) const;

    std::vector<uint8_t> m_bytes;
    uint32_t m_base = 0;
};

} // namespace Gecko
