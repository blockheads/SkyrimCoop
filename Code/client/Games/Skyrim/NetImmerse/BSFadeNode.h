#pragma once

#include <NetImmerse/NiNode.h>

#include <cstddef>

// CommonLibSSE-NG BSFadeNode: an actor's or object's root, faded in and out by distance.
struct BSFadeNode : NiNode
{
    uint8_t padNode[0x130 - sizeof(NiNode)];
    float currentFade; // 0 invisible, 1 fully drawn (BSFadeNode__IsFadedIn 19384 reads it)
};
static_assert(offsetof(BSFadeNode, currentFade) == 0x130);
