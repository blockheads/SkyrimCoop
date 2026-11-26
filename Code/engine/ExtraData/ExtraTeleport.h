#pragma once

#include "ExtraData/ExtraData.h"

// A load door's destination (CommonLibSSE-NG DoorTeleportData; GetLockData 20223 reads linkedDoor first).
struct DoorTeleportData
{
    uint32_t linkedDoor; // handle
    float position[3];
    float rotation[3];
    uint8_t flags;
};

static_assert(offsetof(DoorTeleportData, position) == 0x4);
static_assert(offsetof(DoorTeleportData, rotation) == 0x10);
static_assert(offsetof(DoorTeleportData, flags) == 0x1C);

// CommonLibSSE-NG ExtraTeleport.
struct ExtraTeleport : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Teleport;

    DoorTeleportData* teleportData{};
};

static_assert(sizeof(ExtraTeleport) == 0x18);
