#pragma once

struct PlayerComponent
{
    PlayerComponent(uint32_t aId)
        : Id(aId)
    {
    }

    uint32_t Id;
};
