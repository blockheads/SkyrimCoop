#pragma once

#include <Structs/GraphOps.h>
#include <Structs/Perks.h>

// What a request and its notification carry when a relay passes the request on unchanged (the
// host's authority, Code/client/Services/Host/HostAuthority.cpp). Both messages inherit the payload, so its
// fields and their bytes are written once.

#include <Structs/RecordPart.h>
#include <Structs/ActorWhereabouts.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Recipe.h>
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

struct ActorValueChanges
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorValueChanges&) const noexcept = default;

    uint32_t Id{};
    std::map<uint32_t, float> Values{};
    std::map<uint32_t, float> TemporaryValues{};
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

// A player's name in the party's lists, when its character is renamed or the host loads another (PlayerId: the
// notification's).
struct PlayerName
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PlayerName&) const noexcept = default;

    uint32_t PlayerId{};
    std::string Name{};
};

// An actor's display name, when the host's game changed it (a quest alias's name set or cleared).
struct ActorName
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorName&) const noexcept = default;

    uint32_t ActorId{};
    std::string Name{};
};

struct Mount
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Mount&) const noexcept = default;

    uint32_t RiderId{};
    uint32_t MountId{};
};

// A claim on one of the host's actors (HostClaims): the game of ForId's owner holds it (a rider, a kill move's
// attacker) until it gives it back. DeathOnGiveBack: its death waits for that (the copy's deferred kill kept).
struct ActorClaim
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;
    bool operator==(const ActorClaim&) const noexcept = default;
    uint32_t ActorId{};
    uint32_t ForId{};
    bool DeathOnGiveBack{};
};

// A claimed actor back to the host, with its holder's current placement (a native travel's implicit dismount too).
struct ActorGiveBack
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;
    bool operator==(const ActorGiveBack&) const noexcept = default;
    uint32_t ActorId{};
    uint32_t ForId{}; // the host's: whom the claim was for, the killer of a killed one
    ActorWhereabouts Whereabouts;
    // The holder's last graph update sent (an exit pair's PairEnd due with it, 32797): the host's game takes the actor
    // once its copy has run that far (ReplayState::TakesOverMount); 0 at once.
    uint64_t Tick{};
    bool Killed{}; // a kill counted under its hold (DeathOnGiveBack): the host's game kills it
};

// A claimed pair (a kill move): its starter and its partner, session ids.
struct Pair
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Pair&) const noexcept = default;

    enum Refusal : uint8_t
    {
        kNone,
        kCrossed,    // either actor is in a claimed pair already: the claimer's blow is dropped
        kCannotPair, // the partner is dead or its graph does not run: the claimer swings plainly
    };

    uint32_t ActorId{};
    uint32_t PartnerId{};
    uint8_t Reason{}; // a refusal's
    uint64_t Tick{};  // the claimed action's
};

// One part of an actor's record, from its owner (docs/design/one-simulator.md, piece 1).
struct Record
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Record&) const noexcept = default;

    uint32_t Id{}; // the actor's session id
    RecordPart Part{};
};

// A game whose copy looks wrong asks the actor's owner for a part again (R3).
struct RecordAsk
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const RecordAsk&) const noexcept = default;

    uint32_t Id{};
    RecordKind Kind{};
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
// without, at the host's tick (unreliable). From a guest: the bodies its player holds or moved until they rest there
// and its player's ragdoll (unreliable), or the last of what it held as its player lets go (reliable).
struct BodyFrames
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const BodyFrames&) const noexcept = default;

    uint64_t Tick{};
    std::vector<BodyFrame> Frames;
    bool Held{}; // a player's hold: every other game keyframes it; else played steered
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
    // The host's game put them there itself (a script's or the console's move): a mover's claim on them ends.
    bool Placed{};
};

// The party's gold (docs/design/party-purse.md): every player carries this many of Gold001 (0xF).
struct PartyGold
{
    static inline const GameId kGold{0, 0xF};

    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PartyGold&) const noexcept = default;

    uint32_t Gold{};
    // The change for this game's HUD to show ("Gold (40) removed"); 0 on the game whose player made it (the engine
    // showed it there) and for a purse sent with no change (a join, a load).
    int32_t Shown{};
};

// A party item (docs/design/party-items.md): every player carries Item.Count of that stack ("[P] <name>", its
// extra data), 0 for none.
struct PartyItem
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PartyItem&) const noexcept = default;

    Inventory::Entry Item{};
    // The change for this game's HUD to show, as PartyGold's.
    int32_t Shown{};
};

// Every party item (a join): every player carries these stacks, and no other "[P] " stack.
struct PartyItems
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const PartyItems&) const noexcept = default;

    std::vector<Inventory::Entry> Items;
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

// Player-made potions' recipes (Recipe): before its first message naming one, a game not yet told gets it, a guest's
// to the host and the host's to each guest (docs/design/crafted-potions.md).
struct Recipes
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const Recipes&) const noexcept = default;

    std::vector<Recipe> Known;
};
// A player's /bug (docs/design/bug-reports.md): the text to the host, which names the report and sends it to every
// game, each writing its own part.
struct BugReport
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const BugReport&) const noexcept = default;

    std::string Text;
    std::string Id;          // the host's: every game's part goes under it
    std::string By;          // the player who typed it
    uint64_t SessionTick{};  // the host's clock when it got it
};
// A dev chat command (docs/design/dev-commands.md): the line as typed; the host's names who typed it.
struct DevCommand
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const DevCommand&) const noexcept = default;

    std::string Line;
    std::string By;
};
// A game's player pressed through the NPC's line: the host's game, which says it, moves on.
struct DialogueSkip
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const DialogueSkip&) const noexcept = default;

    uint32_t Conversation{};
    uint32_t Step{};
};
// The conversation's one highlight (docs/design/shared-dialogue.md): a game's player moved it, every other menu in
// the conversation shows it there. Index: the option's place in the step's merged list.
struct DialogueHighlight
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const DialogueHighlight&) const noexcept = default;

    uint32_t Conversation{};
    uint32_t Step{};
    uint32_t Index{};
    uint32_t PlayerId{}; // the host's: who moved it
    uint32_t Seq{};      // the mover's count of its moves: its own echo counts only for its latest
};
// An owner game's graph inputs for its actors since its last batch (docs/design/graph-input-stream.md).
struct GraphOpsBatch
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const GraphOpsBatch&) const noexcept = default;

    GraphOps Ops{}; // emptied if it does not decode
};
// A copy's game asks the owner for a sync point: its copy has none yet, or its stream had a gap.
struct GraphSync
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const GraphSync&) const noexcept = default;

    uint32_t Id{};
};
// An owner's graph right after one of its updates: where its copies' replay starts.
struct GraphSyncPoint
{
    void SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept;
    void DeserializePayload(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const GraphSyncPoint&) const noexcept = default;

    uint32_t Id{};
    uint32_t Update{};  // its updates so far: the ops from this update index on follow it
    uint64_t Tick{};    // the owner's frame tick then
    std::string State;  // its whole graph (Skyrim: Havok's internal state, Games/Animation/GraphState.h)
    std::vector<uint32_t> Words; // its variables, exact
    GraphSeed Seed{};
    // Its next update walks its active nodes again (stateOrTransitionChanged: its last generate pass changed a
    // blender's children). Not node state: Havok's restore clears it.
    bool ActiveNodesChanged{};
    uint32_t Project{}; // its behaviour project's name hashed: a copy with another graph (a leveled base not yet its owner's) waits
    // Each clip set its process holds: (the set, ActionClips::Set; the event loaded into it). Its clips play them.
    std::vector<std::pair<uint8_t, uint16_t>> ClipSets;
    // The props its graph shows (AnimationObjects: ANIO form id, drawn): loaded by triggers the point skips past.
    std::vector<std::pair<uint32_t, bool>> AnimObjects;
};
} // namespace Payloads
