#include "memory.h"
#include "guest_flat_memory.h"
#include "gx_panic.h"
#include "recomp_mod_loader.h"
#include "net/local_server.h"

#include <cstdlib>

// Use the production flat guest window and inline big-endian accessors. Only
// hardware reporting and device reset are replaced; no Wii code is executed.
void PanicLegacyGx(const char*, uint32_t, const CpuContext*) noexcept { std::abort(); }
namespace RecompMod { uint32_t CurrentTranslatedExecutionAddress() noexcept { return 0; } }
namespace LocalOnlineServer {
Mode g_testMode = Mode::Offline;
bool g_testServer = false;
Mode GetMode() { return g_testMode; }
bool IsLocalServerRunning() { return g_testServer; }
}

Memory::AccessViolation::AccessViolation(uint32_t address, size_t length, std::string_view reason)
    : std::runtime_error(std::string(reason)), address_(address), length_(length), reason_(reason) {}

void Memory::Init(size_t size) {
    if (size != kMem1Size)
        throw std::runtime_error("test MEM1 must be 24 MiB");
    GuestFlat::Initialize();
}
uint8_t* Memory::GetPointer(uint32_t address) { return GetPointer(address, 1); }
uint8_t* Memory::GetPointer(uint32_t address, size_t length) {
    if (auto* p = GuestFlat::HostPointer(address, length))
        return p;
    throw AccessViolation(address, length, "outside mapped guest RAM");
}
bool Memory::Contains(uint32_t address, size_t length) { return GuestFlat::IsMapped(address, length); }
