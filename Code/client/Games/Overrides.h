#pragma once

template <class T> struct ScopedOverride
{
    ScopedOverride() { s_refCount++; }

    ~ScopedOverride() { s_refCount--; }

    TP_NOCOPYMOVE(ScopedOverride);

    static bool IsOverriden() { return s_refCount > 0; }

private:
#ifdef __MINGW32__
    // Keep the constant initializer visible at every call site. With separate
    // explicit specializations, GCC emits calls to undefined weak TLS init
    // helpers; PE linkers resolve those into invalid executable addresses.
    inline static thread_local uint32_t s_refCount = 0;
#else
    static thread_local uint32_t s_refCount;
#endif
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
struct Quest
{
};
struct SpellCast
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
using ScopedQuestOverride = ScopedOverride<details::Quest>;
using ScopedSpellCastOverride = ScopedOverride<details::SpellCast>;
