#pragma once

#include <cstdint>

// An actor's head was built and attached, or its state updated (TESNPC::UpdateHeadState 24724): a new head has no
// painted face tints.
struct ActorHeadUpdateEvent
{
    uint32_t FormId{};
};
