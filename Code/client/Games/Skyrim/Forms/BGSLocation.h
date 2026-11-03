#pragma once

#include <Forms/TESForm.h>

// A location (LCTN; CommonLibSSE-NG BGSLocation).
struct BGSLocation : TESForm
{
    uint8_t unk20[0x48 - 0x20]; // its full name and keywords
    BGSLocation* pParent;       // 48, what papyrus Location::IsChild walks (55215)
    uint8_t pad50[0x60 - 0x50];
    uint32_t worldLocMarker;    // 60, CommonLib BGSLocation::worldLocMarker (MNAM)

    // aLocation, or a location inside it.
    [[nodiscard]] bool Holds(const BGSLocation* apLocation) const noexcept
    {
        for (; apLocation; apLocation = apLocation->pParent)
            if (apLocation == this)
                return true;
        return false;
    }
};
static_assert(offsetof(BGSLocation, pParent) == 0x48);

static_assert(offsetof(BGSLocation, worldLocMarker) == 0x60);
