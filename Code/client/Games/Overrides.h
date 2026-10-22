#pragma once

#include <NoCopyMove.h>
#include <TESObjectREFR.h>

template <class T> struct ScopedOverride
{
    ScopedOverride() { s_refCount++; }

    ~ScopedOverride() { s_refCount--; }

    NO_COPY_MOVE(ScopedOverride);

    static bool IsOverriden() { return s_refCount > 0; }

private:
    static inline thread_local uint32_t s_refCount = 0;
};

namespace details
{
struct Equip
{
};
struct Unequip
{
};
struct SaveLoad
{
};
struct Experience
{
};
struct Activate
{
};
struct Inventory
{
};
struct ExtraData
{
};
struct SpellCast
{
};
struct PlayerLock
{
};
struct ItemRemoval
{
};
struct HostChange
{
};
} // namespace details

using ScopedReferencesOverride = ScopedOverride<TESObjectREFR>;
using ScopedEquipOverride = ScopedOverride<details::Equip>;
using ScopedUnequipOverride = ScopedOverride<details::Unequip>;
using ScopedSaveLoadOverride = ScopedOverride<details::SaveLoad>;
using ScopedExperienceOverride = ScopedOverride<details::Experience>;
using ScopedActivateOverride = ScopedOverride<details::Activate>;
using ScopedInventoryOverride = ScopedOverride<details::Inventory>;
using ScopedExtraDataOverride = ScopedOverride<details::ExtraData>;
using ScopedSpellCastOverride = ScopedOverride<details::SpellCast>;
// The player's own lock change happens inside (a lockpick, a key): the lock sync tells it from the world's.
using ScopedPlayerLockOverride = ScopedOverride<details::PlayerLock>;
// An item leaves an actor: a copy may unequip it, but not equip another (the engine picks one for an NPC).
using ScopedItemRemovalOverride = ScopedOverride<details::ItemRemoval>;
// A guest applying the host's object change: our object hooks raise no events for it.
using ScopedHostChangeOverride = ScopedOverride<details::HostChange>;
