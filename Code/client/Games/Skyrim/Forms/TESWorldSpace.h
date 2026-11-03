#pragma once

#include <Forms/TESForm.h>

#include <Components/TESFullName.h>
#include <Forms/TESObjectCELL.h>

struct TESWorldSpace : TESForm
{
    // aX and aY are coordinates, not positions
    TESObjectCELL* LoadCell(int32_t aXCoordinate, int32_t aYCoordinate) noexcept;

    TESFullName fullName;
    uint8_t pad30[0x158 - 0x30];
    TESWorldSpace* parentWorld; // CommonLib TESWorldSpace::parentWorld (WNAM)
};

static_assert(offsetof(TESWorldSpace, parentWorld) == 0x158);
