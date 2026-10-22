#pragma once

#include <Structs/ActionEvent.h>

struct LocalComponent
{
    LocalComponent(uint32_t aId) noexcept
        : Id(aId)
    {
    }

    uint32_t Id;
    ActionEvent CurrentAction;
};
