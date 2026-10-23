#pragma once

// What a request and its notification carry when a relay passes the request on unchanged (the
// host's authority, Code/client/Services/Host/HostAuthority.cpp). Both messages inherit the payload, so its
// fields and their bytes are written once.

#include <Structs/ActorAiState.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/ScriptValue.h>
#include <Wire/Buffer.h>

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

// An actor's AI state, when its owner's changed.
struct AiState
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const AiState&) const noexcept = default;

    uint32_t SessionId{};
    ActorAiState State{};
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

// An effect's visuals that play once (no FX Persist) just started on one of the owner's actors: its copies play them.
struct EffectFlash
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const EffectFlash&) const noexcept = default;

    uint32_t SessionId{};
    GameId EffectId{};
};

// A native on "the player" the host's script called for a guest's character, which that guest's game runs instead
// (docs/design/party-model.md, rule 2).
struct PlayerNative
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PlayerNative&) const noexcept = default;

    uint32_t SessionId{}; // the guest's character
    ScriptCall Call{};    // a global native's
    uint32_t ReplyId{};   // a native returning a value: the host's script waits for PlayerNativeResult under this id; 0: none
};

// What a native on the player returned on the guest's game, for the host's script waiting on it (PlayerNative::ReplyId).
struct PlayerNativeResult
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PlayerNativeResult&) const noexcept = default;

    uint32_t ReplyId{};
    ScriptValue Result{}; // None also when it did not run there (the guest left, an argument not there)
};

// A script's call on one game's controls or screen (a fade, the camera, a shake, music, a video), which every other
// game runs on its own player and screen (docs/design/cutscenes.md).
struct PartyNative
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PartyNative&) const noexcept = default;

    ScriptCall Call{}; // its self: an image-space modifier, a music type; None for a global native
};

// A guest's own script changed the world (a quest's stage): the host's game makes the call instead, for that guest
// (docs/design/world-state-sync.md, "Only the host's game decides").
struct WorldCall
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const WorldCall&) const noexcept = default;

    uint32_t SessionId{}; // the guest's character, the host fills it in
    ScriptCall Call{};
};
} // namespace Payloads
