#include "gecko/gecko_memory_guest.h"

#include "memory.h"

namespace Gecko {

bool GuestMemory::Aligned(uint32_t address, uint32_t size) {
    if (size == 0)
        return false;

    const uint32_t alignment = (size >= 4u) ? 4u : (size >= 2u ? 2u : 1u);

    return (address & (alignment - 1u)) == 0;
}

bool GuestMemory::InGuestRam(uint32_t address, uint32_t size) {
    const uint64_t start = address;
    const uint64_t end = start + size;

    if (end < start)
        return false;

    const bool inMem1 = start >= ::Memory::kMem1CachedBase && end <= uint64_t(::Memory::kMem1CachedBase) + ::Memory::kMem1Size;
    const bool inMem1Uncached = start >= ::Memory::kMem1UncachedBase && end <= uint64_t(::Memory::kMem1UncachedBase) + ::Memory::kMem1Size;
    const bool inMem2 = start >= ::Memory::kMem2CachedBase && end <= uint64_t(::Memory::kMem2CachedBase) + ::Memory::kMem2Size;
    const bool inMem2Uncached = start >= ::Memory::kMem2UncachedBase && end <= uint64_t(::Memory::kMem2UncachedBase) + ::Memory::kMem2Size;

    return inMem1 || inMem1Uncached || inMem2 || inMem2Uncached;
}

bool GuestMemory::Read8(uint32_t address, uint8_t& value) const {
    if (!InGuestRam(address, 1) || !::Memory::Contains(address, 1))
        return false;

    value = ::Memory::Read8(address);
    return true;
}

bool GuestMemory::Read16(uint32_t address, uint16_t& value) const {
    if (!Aligned(address, 2) || !InGuestRam(address, 2) || !::Memory::Contains(address, 2))
        return false;

    value = ::Memory::Read16(address);
    return true;
}

bool GuestMemory::Read32(uint32_t address, uint32_t& value) const {
    if (!Aligned(address, 4) || !InGuestRam(address, 4) || !::Memory::Contains(address, 4))
        return false;

    value = ::Memory::Read32(address);
    return true;
}

bool GuestMemory::Write8(uint32_t address, uint8_t value) {
    if (!InGuestRam(address, 1) || !::Memory::Contains(address, 1))
        return false;

    ::Memory::Write8(address, value);
    return true;
}

bool GuestMemory::Write16(uint32_t address, uint16_t value) {
    if (!Aligned(address, 2) || !InGuestRam(address, 2) || !::Memory::Contains(address, 2))
        return false;

    ::Memory::Write16(address, value);
    return true;
}

bool GuestMemory::Write32(uint32_t address, uint32_t value) {
    if (!Aligned(address, 4) || !InGuestRam(address, 4) || !::Memory::Contains(address, 4))
        return false;

    ::Memory::Write32(address, value);
    return true;
}

bool GuestMemory::Valid(uint32_t address, uint32_t size) const {
    if (!Aligned(address, size) || !InGuestRam(address, size) || !::Memory::Contains(address, size))
        return false;

    return true;
}

} // namespace Gecko
