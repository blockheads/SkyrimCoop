#pragma once

#include <cstdint>

/**
 * @brief Dispatched when a spell has been cast locally.
 *
 * All fields are pre-extracted primitives so the event carries no SKSE types
 * and can be consumed by services compiled without the Games/Skyrim include path.
 */
struct SpellCastEvent
{
    SpellCastEvent(uint32_t aCasterFormId, uint32_t aSpellId, uint32_t aDesiredTargetID,
                   uint32_t aCastingSource, bool aIsDualCasting, bool aHasNiNode)
        : CasterFormId(aCasterFormId)
        , SpellId(aSpellId)
        , DesiredTargetID(aDesiredTargetID)
        , CastingSource(aCastingSource)
        , IsDualCasting(aIsDualCasting)
        , HasNiNode(aHasNiNode)
    {
    }

    uint32_t CasterFormId{};
    uint32_t SpellId{};
    uint32_t DesiredTargetID{};
    uint32_t CastingSource{};
    bool IsDualCasting{false};
    bool HasNiNode{false};
};
