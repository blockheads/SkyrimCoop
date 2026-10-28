#pragma once

#include <Games/Primitives.h>

struct TESObjectREFR;
struct TESForm;

enum class ExtraDataType : uint32_t
{
    None = 0,
    ContainerChanges = 0x15,
    Worn = 0x16,
    WornLeft = 0x17,
    Package = 0x19, // CommonLibSSE-NG ExtraPackage: the form package beneath a runtime procedure
    ReferenceHandle = 0x1C,
    Ownership = 0x21,
    Count = 0x24,
    Health = 0x25,
    Charge = 0x28,
    Teleport = 0x2B,
    LeveledCreature = 0x2D,
    CannotWear = 0x3D,
    Poison = 0x3E,
    Faction = 0x5B,
    OpenCloseActivateRef = 0x68,
    AliasInstanceArray = 0x88,
    OutfitItem = 0x8E,
    TextDisplayData = 0x99,
    Enchantment = 0x9B,
    Soul = 0x9C,
    Interaction = 0xA9, // a mount's rider and a rider's mount (ExtraInteraction)
};

struct BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::None;

    virtual ~BSExtraData() = 0;
    virtual ExtraDataType GetType() const noexcept = 0;

    BSExtraData* next{};
};
