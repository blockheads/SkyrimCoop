#pragma once

// What a request and its notification carry when a relay passes the request on unchanged (the
// host's bridge, Code/client/Services/HostBridge.cpp). Both messages inherit the payload, so its
// fields and their bytes are written once.

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

namespace Payloads
{
// The owner's full worn equipment for one actor, re-sent periodically so a copy that drifted
// (missed or undone change) converges back to the owner's state.
struct EquipmentSnapshot
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const EquipmentSnapshot&) const noexcept = default;

    uint32_t ServerId{};
    Inventory Equipment{};
};

struct DrawWeapon
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const DrawWeapon&) const noexcept = default;

    uint32_t Id{};
    bool IsWeaponDrawn{};
};

struct ActorValueChanges
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorValueChanges&) const noexcept = default;

    uint32_t Id{};
    TiltedPhoques::Map<uint32_t, float> Values{};
};

struct ActorMaxValueChanges
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorMaxValueChanges&) const noexcept = default;

    uint32_t Id{};
    TiltedPhoques::Map<uint32_t, float> Values{};
};

struct DeathStateChange
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const DeathStateChange&) const noexcept = default;

    uint32_t Id{};
    bool IsDead{};
};

struct HealthChange
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const HealthChange&) const noexcept = default;

    uint32_t Id{};
    float DeltaHealth{};
    bool HasAttacker{};
    uint32_t AttackerId{};
};

struct SpellCast
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const SpellCast&) const noexcept = default;

    uint32_t CasterId{};
    GameId SpellFormId{};
    int32_t CastingSource{};
    bool IsDualCasting{};
    uint32_t DesiredTarget{};
};

struct InterruptCast
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const InterruptCast&) const noexcept = default;

    uint32_t CasterId{};
    int32_t CastingSource{};
};

struct ProjectileLaunch
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const ProjectileLaunch&) const noexcept = default;

    uint32_t ShooterID{};
    float OriginX{};
    float OriginY{};
    float OriginZ{};
    GameId ProjectileBaseID{};
    GameId WeaponID{};
    GameId AmmoID{};
    float ZAngle{};
    float XAngle{};
    float YAngle{};
    GameId ParentCellID{};
    GameId SpellID{};
    int32_t CastingSource{};
    int32_t Area{};
    float Power{};
    float Scale{};
    bool AlwaysHit{};
    bool NoDamageOutsideCombat{};
    bool AutoAim{};
    bool DeferInitialization{};
    bool ForceConeOfFire{};
    bool UnkBool1{};
    bool UnkBool2{};
};

struct AddTarget
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const AddTarget&) const noexcept = default;

    uint32_t TargetId{};
    uint32_t CasterId{};
    GameId SpellId{};
    GameId EffectId{};
    float Magnitude{};
    bool IsDualCasting{};
    bool ApplyHealPerkBonus{};
    bool ApplyStaminaPerkBonus{};
};

struct RemoveSpell
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const RemoveSpell&) const noexcept = default;

    uint32_t TargetId{};
    GameId SpellId{};
};

struct Mount
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const Mount&) const noexcept = default;

    uint32_t RiderId{};
    uint32_t MountId{};
};

struct NewPackage
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const NewPackage&) const noexcept = default;

    uint32_t ActorId{};
    GameId PackageId{};
};

struct Dialogue
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const Dialogue&) const noexcept = default;

    uint32_t ServerId{};
    TiltedPhoques::String SoundFilename{};
};

struct Subtitle
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const Subtitle&) const noexcept = default;

    uint32_t ServerId{};
    TiltedPhoques::String Text{};
    uint32_t TopicFormId{};
};

struct SyncExperience
{
    void SerializePayload(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const SyncExperience&) const noexcept = default;

    float Experience{};
};
} // namespace Payloads
