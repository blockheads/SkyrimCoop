#pragma once

#include <Structs/Inventory.h>

struct RemoteComponent
{
    RemoteComponent(uint32_t aId, uint32_t aRefId) noexcept
        : Id(aId)
        , CachedRefId(aRefId)
    {
    }

    uint32_t Id;
    uint32_t CachedRefId;
};
