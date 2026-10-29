#pragma once

#include <Structs/Perks.h>

// What a request and its notification carry when a relay passes the request on unchanged (the
// host's authority, Code/client/Services/Host/HostAuthority.cpp). Both messages inherit the payload, so its
// fields and their bytes are written once.

#include <Structs/ActorAiState.h>
#include <Structs/ActorWhereabouts.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/ScriptValue.h>
#include <Wire/Buffer.h>
#include <optional>

namespace Payloads
{
struct MeleeStagger
{
    uint32_t HitteeId{};
    uint32_t HitterId{};
    float Magnitude{};
    bool operator==(const MeleeStagger&) const noexcept = default;
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;
};

struct ActorPerks
{
    uint32_t SessionId{};
    Perks Content;
    bool operator==(const ActorPerks&) const noexcept = default;
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;
};

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
    // The owner's death action: a death waits for it, a resurrection corrects it even if it arrives later.
    std::optional<uint64_t> DeathActionTick;
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
// Ownership returns with the last owner's current placement, including a native travel's implicit dismount.
struct Dismount : Mount
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;
    bool operator==(const Dismount&) const noexcept = default;
    ActorWhereabouts Whereabouts;
};

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

// A script's call one game sends to others, as PartyRules::RouteOf decided.
struct RoutedCall
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const RoutedCall&) const noexcept = default;

    CallTo To{};
    uint32_t SessionId{}; // kHost: the sender's character (the host fills it in); kOwner: the guest's character
    uint32_t Source{};    // kOthers: the sending game's player id (the host fills it in), whose cutscene it is
    uint32_t ReplyId{};   // kOwner, a native returning a value: the host's script waits for PlayerNativeResult; kOthers,
                          // a latent call: its key, the sender's stack (CallEnded); 0: none
    ScriptCall Call{};    // its self: an image-space modifier, a music type, an actor; None for a global native
};

// A latent party call returned on the game that made it (a story's Message.Show answered): its copies on the other
// games end with the same result (docs/design/party-calls.md, "A box's answer").
struct CallEnded
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const CallEnded&) const noexcept = default;

    uint32_t Source{}; // the sending game's player id (the host fills it in)
    uint32_t Key{};    // RoutedCall::ReplyId
    ScriptValue Result{};
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

// Physics bodies moving on the host's game (docs/design/physics-sync.md): the frames a guest would guess wrong
// without, at the host's tick (unreliable). From a guest: the bodies its player holds and its player's ragdoll
// (unreliable), or the last of what it held as its player lets go (reliable).
struct BodyFrames
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const BodyFrames&) const noexcept = default;

    uint64_t Tick{};
    std::vector<BodyFrame> Frames;
};

// Bodies that fell asleep on their owner's game, where they stay (reliable). Asked by a guest (BodyPoseRequest):
// bodies its own physics moved that the host's did not, where they went; the host answers with its own.
struct BodyRests
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const BodyRests&) const noexcept = default;

    uint64_t Tick{};
    std::vector<BodyPose> Rests;
};

// A guest's player pushed the host's bodies in its game (reliable): the host pushes its own alike.
struct BodyPushes
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const BodyPushes&) const noexcept = default;

    std::vector<BodyPush> Pushes;
};

// The party's gold (docs/design/party-purse.md): every player carries this many of Gold001 (0xF).
struct PartyGold
{
    static inline const GameId kGold{0, 0xF};

    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PartyGold&) const noexcept = default;

    uint32_t Gold{};
};

// The quests that are one per player (docs/design/system-quests.md), learned by a game whose player's own script
// called into one: a guest's to the host, the host's whole list to every game.
struct SystemQuests
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const SystemQuests&) const noexcept = default;

    std::vector<GameId> Quests;
};
} // namespace Payloads
