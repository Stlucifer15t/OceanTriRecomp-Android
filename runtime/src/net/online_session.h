#pragma once

#include "memory.h"
#include "net/local_server.h"

#include <cstdint>

// Game-thread-only view of whether the guest is in an online/City session.
//
// Do NOT use system_w+0x24 (0x8065AA84) for this. That byte is the scene/game
// mode used by StartGameModeTask: 1 field, 2 village. Offline Moga Village
// gameplay therefore has value 2, which previously false-positived the cheat
// online guard. The reliable PAL signals for the online City are the play mode
// at system_w+0x25 (4 online/City, 5 offline village) and the City-choice flag
// at system_w+0x7D4 set by the mode menu / village boot.
namespace OnlineSession {

inline constexpr uint32_t kSystem = 0x8065AA60u;
inline constexpr uint32_t kPlayMode = kSystem + 0x25u;      // u8: 4 online/City, 5 offline village
inline constexpr uint32_t kCityFlag = kSystem + 0x7D4u;     // u8: 1 when the City choice was taken
inline constexpr uint8_t kPlayModeOnline = 4;

inline bool Active() noexcept {
    if (LocalOnlineServer::IsLocalServerRunning())
        return true;
    if (Memory::Contains(kCityFlag, 1) && Memory::Read8(kCityFlag) == 1)
        return true;
    return Memory::Contains(kPlayMode, 1) && Memory::Read8(kPlayMode) == kPlayModeOnline;
}

} // namespace OnlineSession
