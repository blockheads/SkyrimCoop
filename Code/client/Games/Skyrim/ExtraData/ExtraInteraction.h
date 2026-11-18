#pragma once

#include "ExtraData.h"

struct RefrInteraction;

// A rider's or mount's link to the other (CommonLibSSE-NG ExtraInteraction); a horse holds one (12074).
struct ExtraInteraction : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Interaction;

    RefrInteraction* interaction{}; // BSTSmartPointer
};

static_assert(sizeof(ExtraInteraction) == 0x18);
