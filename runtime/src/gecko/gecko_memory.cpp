#include "gecko/gecko_memory.h"

#include <cstring>
#include <utility>

namespace Gecko {

FlatMemory::FlatMemory(uint32_t base, size_t size, std::vector<uint8_t> bytes)
    : m_base(base) {
    m_bytes = std::move(bytes);
    m_bytes.resize(size, 0);
}

bool FlatMemory::InRange(uint32_t address, uint32_t size) const {
    if (address < m_base)
        return false;

    const uint32_t offset = address - m_base;

    if (size == 0)
        return offset <= m_bytes.size();

    // uint64_t so a large size cannot overflow the addition.
    if (uint64_t(offset) + size > m_bytes.size())
        return false;

    return true;
}

bool FlatMemory::Read8(uint32_t address, uint8_t& value) const {
    if (!InRange(address, 1))
        return false;

    value = m_bytes[address - m_base];
    return true;
}

bool FlatMemory::Read16(uint32_t address, uint16_t& value) const {
    if ((address & 1u) != 0)
        return false;

    if (!InRange(address, 2))
        return false;

    uint8_t low = m_bytes[address - m_base];
    uint8_t high = m_bytes[address - m_base + 1];
    value = uint16_t(low) | uint16_t(high) << 8;
    return true;
}

bool FlatMemory::Read32(uint32_t address, uint32_t& value) const {
    if ((address & 3u) != 0)
        return false;

    if (!InRange(address, 4))
        return false;

    const uint8_t* bytes = m_bytes.data() + (address - m_base);
    value = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    return true;
}

bool FlatMemory::Write8(uint32_t address, uint8_t value) {
    if (!InRange(address, 1))
        return false;

    m_bytes[address - m_base] = value;
    return true;
}

bool FlatMemory::Write16(uint32_t address, uint16_t value) {
    if ((address & 1u) != 0)
        return false;

    if (!InRange(address, 2))
        return false;

    m_bytes[address - m_base] = uint8_t(value);
    m_bytes[address - m_base + 1] = uint8_t(value >> 8);
    return true;
}

bool FlatMemory::Write32(uint32_t address, uint32_t value) {
    if ((address & 3u) != 0)
        return false;

    if (!InRange(address, 4))
        return false;

    uint8_t* bytes = m_bytes.data() + (address - m_base);
    bytes[0] = uint8_t(value);
    bytes[1] = uint8_t(value >> 8);
    bytes[2] = uint8_t(value >> 16);
    bytes[3] = uint8_t(value >> 24);
    return true;
}

bool FlatMemory::Valid(uint32_t address, uint32_t size) const {
    return InRange(address, size);
}

} // namespace Gecko
