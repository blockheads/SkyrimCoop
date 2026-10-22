#pragma once

// What a request and its notification carry when a relay passes the request on unchanged (the
// host's authority, Code/client/Services/Host/HostAuthority.cpp). Both messages inherit the payload, so its
// fields and their bytes are written once.

#include <Structs/GameId.h>
#include <Structs/Inventory.h>

namespace Payloads
{
// The owner's full worn equipment for one actor, re-sent periodically so a copy that drifted
// (missed or undone change) converges back to the owner's state.
struct EquipmentSnapshot
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const EquipmentSnapshot&) const noexcept = default;

    uint32_t SessionId{};
    Inventory Equipment{};
};

struct DrawWeapon
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const DrawWeapon&) const noexcept = default;

    uint32_t Id{};
    bool IsWeaponDrawn{};
};

struct ActorValueChanges
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorValueChanges&) const noexcept = default;

    uint32_t Id{};
    std::map<uint32_t, float> Values{};
};

struct ActorMaxValueChanges
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorMaxValueChanges&) const noexcept = default;

    uint32_t Id{};
    std::map<uint32_t, float> Values{};
};

struct DeathStateChange
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const DeathStateChange&) const noexcept = default;

    uint32_t Id{};
    bool IsDead{};
};

struct HealthChange
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const HealthChange&) const noexcept = default;

    uint32_t Id{};
    float DeltaHealth{};
    bool HasAttacker{};
    uint32_t AttackerId{};
};

struct SpellCast
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const SpellCast&) const noexcept = default;

    uint32_t CasterId{};
    GameId SpellFormId{};
    int32_t CastingSource{};
    bool IsDualCasting{};
    uint32_t DesiredTarget{};
};

struct InterruptCast
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const InterruptCast&) const noexcept = default;

    uint32_t CasterId{};
    int32_t CastingSource{};
};

struct ProjectileLaunch
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

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
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

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
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const RemoveSpell&) const noexcept = default;

    uint32_t TargetId{};
    GameId SpellId{};
};

struct Mount
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Mount&) const noexcept = default;

    uint32_t RiderId{};
    uint32_t MountId{};
};

struct NewPackage
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const NewPackage&) const noexcept = default;

    uint32_t ActorId{};
    GameId PackageId{};
};

struct Dialogue
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Dialogue&) const noexcept = default;

    uint32_t SessionId{};
    std::string SoundFilename{};
};

struct Subtitle
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Subtitle&) const noexcept = default;

    uint32_t SessionId{};
    std::string Text{};
    uint32_t TopicFormId{};
    bool Force{}; // shown at any distance (the engine's forceDisplay, as for a line said to the player)
};

struct SyncExperience
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const SyncExperience&) const noexcept = default;

    float Experience{};
};
// An actor's active magic effects that show on it (a hit shader or hit art: burning, frost,
// a healing glow), from its owner. Copies play only those visuals; the owner applies the effects.
struct ActiveEffects
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActiveEffects&) const noexcept = default;

    uint32_t SessionId{};
    std::vector<GameId> EffectIds{};
};
} // namespace Payloads
