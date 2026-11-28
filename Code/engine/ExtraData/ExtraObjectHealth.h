#pragma once

#include "ExtraData/ExtraData.h"

// A destructible's health (CommonLibSSE-NG ExtraObjectHealth): set by its damage (BGSDestructibleObjectForm::DamageObject 14158),
// none while undamaged.
struct ExtraObjectHealth : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::ObjectHealth;

    float health{};
};

static_assert(offsetof(ExtraObjectHealth, health) == 0x10);
static_assert(sizeof(ExtraObjectHealth) == 0x18);
