#pragma once

#include <Primitives.h>

struct TESForm;

// The player's favorited spells and shouts and their number keys: save global 109 (CommonLibSSE-NG MagicFavorites;
// read in IDA, docs/re/systems/player-save-state.md).
struct MagicFavorites
{
    static MagicFavorites* Get() noexcept;

    virtual ~MagicFavorites();

    uint8_t pad8[0x8];
    GameArray<TESForm*> spells;  // 10: sorted by pointer (52004)
    GameArray<TESForm*> hotkeys; // 28: the form on each number key, null for none (52008)
};
static_assert(offsetof(MagicFavorites, spells) == 0x10);
static_assert(offsetof(MagicFavorites, hotkeys) == 0x28);
static_assert(sizeof(MagicFavorites) == 0x40);
