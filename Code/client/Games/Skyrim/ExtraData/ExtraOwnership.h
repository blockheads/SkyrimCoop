#pragma once

#include "ExtraData.h"

// Who owns the reference: an NPC or a faction (CommonLibSSE-NG ExtraOwnership).
struct ExtraOwnership : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Ownership;

    TESForm* owner{};
};

SKYRIM_STRUCT_ASSERT(sizeof(ExtraOwnership) == 0x18);
