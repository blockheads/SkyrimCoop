#pragma once

#include <array>

#include <cstdint>
#include <Structs/Perks.h>
#include <string>
#include <vector>
#include <Structs/ActorValues.h>
#include <Structs/AnimationVariables.h>
#include <Systems/CopyStep.h>
#include <Structs/Inventory.h>
#include <Structs/Faction.h>
#include <Structs/Factions.h>
#include <Structs/Tints.h>
#include <Structs/ActionEvent.h>
#include <Structs/WeaponState.h>

namespace Payloads
{
struct TrapContact;
struct TrapHit;
}

/// All Actor* and related operations abstracted away from SKSE headers.
/// Implementations must NOT leak any SKSE type into the return type or
/// parameter list — only primitives, std::string, std::vector<T>, and
/// Structs/ types (which have no SKSE dependency).
// An actor's AI state (copy-contract.md, "AI state"): this game's form ids, 0 for none.
struct ActorAiFields
{
    bool operator==(const ActorAiFields&) const noexcept = default;

    uint32_t PackageId{};      // Actor::GetCurrentPackage
    uint32_t SceneId{};        // Actor::GetCurrentScene
    uint8_t CombatState{};     // Actor::GetCombatState
    uint32_t CombatTargetId{}; // Actor::currentCombatTarget
    // HighProcessData::headTrackTarget: what it looks at, for each reason (default, action, script, combat,
    // dialogue, procedure); 0 none. The engine looks at the highest set one (39484).
    std::array<uint32_t, 6> HeadTrackIds{};
    // The furniture it sits or sleeps in (38708), 0 none. A copy's has its graph linked as 39912 links an owner's.
    uint32_t FurnitureId{};
};

// An actor's state beside its graph (one-simulator.md, piece 2): this game's form ids, 0 for none.
struct ActorStateFields
{
    bool operator==(const ActorStateFields&) const noexcept = default;

    uint32_t Flags1{};       // ActorState flags1
    uint32_t Flags2{};       // ActorState flags2
    bool InKillMove{};       // Actor::IsInKillMove (38615)
    uint32_t KillerFormId{}; // Actor::killerHandle (+0x108), set by EnterKillMove (38613)
    bool InPair{};           // in a pair's instance (32808) or its clip (bIsSynced)
    bool Dead{};
    int32_t Noise{};         // the noise its actions make now (actionValue +0x11C, SetActionValue 37711)
};

class IActorBridge
{
public:
    virtual ~IActorBridge() = default;

    // --- Existence / identity ---
    virtual bool ActorExists(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorStillLoading(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDead(uint32_t aFormId) const noexcept = 0;
    virtual bool IsPlayerSummon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsTemporaryActor(uint32_t aFormId) const noexcept = 0;

    // --- Copy / player flags (ActorExtension; words in docs/design/glossary.md) ---
    // Another player's character, shown here.
    virtual bool IsOtherPlayer(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept = 0;
    // Kept by the engine when its cell unloads: another player's copy, which travels with the party.
    // Sets persistence and returns its previous value.
    virtual bool SetActorPersistent(uint32_t aFormId, bool aPersistent = true) noexcept = 0;
    /// True for copies (another game owns them), false for actors this game simulates.
    virtual bool IsActorCopy(uint32_t aFormId) const noexcept = 0;
    // A player's character, here or a copy.
    virtual void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept = 0;

    // --- Ownership / command ---
    virtual uint32_t GetCommandingActor(uint32_t aFormId) const noexcept = 0;

    // --- State ---
    virtual void KillActor(uint32_t aFormId) noexcept = 0;
    // Health damage as the engine's hits take it (attacker 0 = none): at 0 its death rules decide, with a
    // death animation and kill credit, or an essential actor bleeding out. False: not applied, a kill move holds
    // the actor (the kill move kills it).
    virtual bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept = 0;
    virtual void RespawnActor(uint32_t aFormId) noexcept = 0;
    // Brings a dead actor back where it is (the console's `resurrect`), without a Reset.
    virtual void ResurrectActor(uint32_t aFormId) noexcept = 0;
    // Death stops graph updates and speech; resume them after a confirmed resurrection.
    virtual void ResumeActorAnimation(uint32_t aFormId) noexcept = 0;
    virtual void DeleteActor(uint32_t aFormId) noexcept = 0;

    // --- Position / movement ---
    virtual void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept = 0;
    virtual void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept = 0;
    // Resident game-thread operation; no pointers into a reloadable DLL retained. Leave: only its graph's variables
    // (its ragdoll places it); Step: its engine steps it there this frame; Place: put there.
    virtual bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
        const float aRotation[3], float aDirection, const AnimationVariables& aVariables, CopyMove aMove) noexcept = 0;
    virtual void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;
    // Alive and in knock states 1-4.
    [[nodiscard]] virtual bool IsKnockedDown(uint32_t aFormId) const noexcept = 0;
    // It lies in its ragdoll, dead or knocked down: the engine keeps it on the ragdoll (Actor::LiesInRagdoll).
    [[nodiscard]] virtual bool IsLyingInRagdoll(uint32_t aFormId) const noexcept = 0;
    // Its character controller on the ground (hkpCharacterStateType 0): not jumping, falling, swimming or flying.
    [[nodiscard]] virtual bool IsOnGround(uint32_t aFormId) const noexcept = 0;
    // Its capsule collides (filter bit 0x8000 clear): the engine turns that off in furniture (40063), far off, mounted.
    [[nodiscard]] virtual bool CapsuleCollides(uint32_t aFormId) const noexcept = 0;
    // A copy's capsule made to collide or not, as its owner's (CapsuleCollides).
    virtual void SetCapsuleCollision(uint32_t aFormId, bool aCollides) noexcept = 0;
    // The layer its own bodies are on in the physics world, 0 when out of it: the AI puts them there as it sits (40063),
    // and the crosshair takes a seated actor only by them (CrosshairPickData::Pick).
    [[nodiscard]] virtual uint8_t BodiesLayer(uint32_t aFormId) const noexcept = 0;
    // Its graphs step with the physics world, so their ragdoll driver drives its bodies (63562); detached in furniture.
    [[nodiscard]] virtual bool GraphsStepPhysics(uint32_t aFormId) const noexcept = 0;
    // A copy's bodies and graphs put as its owner's are, in the engine's order (40063 detaches the graphs first, 39912
    // takes the bodies out first). A copy runs no AI, so its engine never does it.
    virtual void SetCopyPhysics(uint32_t aFormId, uint8_t aBodiesLayer, bool aGraphsStep) noexcept = 0;
    virtual void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;

    // --- Actor values ---
    virtual float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    // Its base value, before every modifier (ActorValueOwner::GetBaseValue).
    virtual float GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    // Its base made aValue (a copy's: its owner's current value).
    virtual void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept = 0;
    virtual void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept = 0;
    virtual int32_t GetActorValueCount() const noexcept = 0;

    // --- Equipment / weapon ---
    virtual bool IsWeaponDrawn(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept = 0;
    virtual void ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlot) noexcept = 0;
    virtual void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept = 0;

    // --- Actor animation / graph ---
    virtual uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept = 0;
    virtual void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept = 0;
    virtual void GenerateMagicCasters(uint32_t aFormId) noexcept = 0;

    // --- Magic ---
    virtual void SetMagicCasterDualCasting(uint32_t aFormId, uint32_t aCastingSource, bool aDual) noexcept = 0;
    /// Casts immediately from the actor's caster for that source (remote spell cast sync);
    /// aDesiredTargetFormId 0 = no target.
    virtual void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                           uint32_t aDesiredTargetFormId) noexcept = 0;
    virtual void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept = 0;
    /// Spell currently being cast from that source (0 if none).
    virtual uint32_t GetCastingSpellFormId(uint32_t aFormId, uint32_t aCastingSource) const noexcept = 0;
    virtual void RemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId) noexcept = 0;

    // --- Inventory ---
    virtual bool IsBleedingOut(uint32_t aFormId) const noexcept = 0;
    virtual void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept = 0;
    virtual uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept = 0;

    // --- Combat / target ---
    virtual uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept = 0;

    // --- Base form ---
    virtual uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept = 0;
    // Changes this reference's NPC base and reloads its model; false if either form is missing.
    virtual bool SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept = 0;
    // Its name as the game shows it: the reference's name override, else its base's.
    virtual std::string GetActorDisplayName(uint32_t aFormId) const noexcept = 0;
    // An actor's name over its base's, as another game told it (ExtraTextDisplayData).
    virtual void SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept = 0;
    virtual uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept = 0;
    // Its NPC base's weight (TESNPC, 0 to 100); 50 without an NPC base. The engine saves it with the actor
    // (Actor::SaveGame 37649), not in the base's sections, so it travels beside AppearanceBuffer.
    virtual float GetActorBaseWeight(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorBaseLevel(uint32_t aFormId, uint16_t aLevel) noexcept = 0;

    // --- Node / 3D ---
    virtual bool ActorHas3D(uint32_t aFormId) const noexcept = 0;
    // Whether the actor's animation graph can play actions (a copy's replay waits for this).
    virtual bool IsAnimationGraphReady(uint32_t aFormId) const noexcept = 0;

    // One active magic effect on an actor (debugging / desync checks).
    struct ActiveEffectInfo
    {
        uint32_t EffectFormId{};
        uint32_t SpellFormId{};
        uint32_t CasterFormId{};
        bool CasterIsActor{}; // false for an object's cast: a trap, an explosion (its NonActorMagicCaster), a hazard
        float Magnitude{};
        float Duration{};
        float Elapsed{};
        uint32_t Flags{};
    };
    virtual std::vector<ActiveEffectInfo> GetActorActiveEffects(uint32_t aFormId) const noexcept = 0;
    // The game's own reaction to being attacked (fight back, flee, alarm), as when a weapon or
    // spell physically hits. Used by the owner when it applies a hit requested by another game.
    virtual void ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept = 0;
    // Who killed a dead actor (0 if alive or unknown), as Papyrus Actor.GetKiller().
    virtual uint32_t GetKillerFormId(uint32_t aFormId) const noexcept = 0;
    /// Actors in the engine's high process list with loaded 3D, excluding the player.
    virtual std::vector<uint32_t> GetLoadedActorFormIds() const noexcept = 0;

    // --- Actor values (bulk) ---
    virtual void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept = 0;

    // --- Inventory (bulk) ---
    virtual Inventory GetActorInventory(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept = 0;

    // --- Factions ---
    virtual Factions GetActorFactions(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept = 0;

    // --- Weapon draw ---
    virtual void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept = 0;
    // The draw or sheathe's result with no action or animation: for a copy whose graph is already its owner's.
    virtual void ShowActorWeaponDrawn(uint32_t aFormId, bool aDrawn) noexcept = 0;
    virtual WeaponState GetWeaponState(uint32_t aFormId) const noexcept = 0;

    // --- Actor state (ActorStateService) ---
    virtual ActorStateFields GetActorStateFields(uint32_t aFormId) const noexcept = 0;
    // A copy: its flags as given (the caller masks them, ActorStateRules).
    virtual void SetCopyActorStateFlags(uint32_t aFormId, uint32_t aFlags1, uint32_t aFlags2) noexcept = 0;
    // A copy: its owner's noise, held until its owner's ends (ActorStateRules::CopyNoiseTimer).
    virtual void SetCopyNoise(uint32_t aFormId, int32_t aNoise) noexcept = 0;
    // A copy enters a kill move as EnterKillMove (38613) does, its killer aKillerFormId (0: the attacker's half).
    virtual void CopyEnterKillMove(uint32_t aFormId, uint32_t aKillerFormId) noexcept = 0;
    // A copy leaves a kill move without dying: its killer cleared, then EndKillMove (38614).
    virtual void CopyLeaveKillMove(uint32_t aFormId) noexcept = 0;
    // A claimed kill move (one-simulator.md, "Action claims"; kill-moves.md). An NPC taken by this game keeps the copy's deferred
    // kill (true: its clip's KillActor only counts); released, the hold ends and its counted kills are dropped.
    virtual void ActorHoldKill(uint32_t aFormId, bool aHold) noexcept = 0;
    // Its clip killed it under the hold (a counted kill, MiddleHigh +0x2F8 above 1).
    [[nodiscard]] virtual bool ActorKillCounted(uint32_t aFormId) const noexcept = 0;
    // A kill is queued and not run yet (QueueKill 37316's task; MiddleHigh +0x331): a clip's kill counts frames later.
    [[nodiscard]] virtual bool ActorKillQueued(uint32_t aFormId) const noexcept = 0;
    // The host's game kills its NPC a claimed kill move's clip killed, credited to aKillerFormId.
    virtual void KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept = 0;
    // This game's actor ends its pair with a copy: the host refused its claim, or gave another the claim.
    virtual void ActorYieldPair(uint32_t aOwnFormId, uint32_t aCopyFormId) noexcept = 0;
    // The action again with no target, as the engine's own second try after a refused pair (42350); true if taken.
    virtual bool ActorPerformPlainly(uint32_t aFormId, uint32_t aActionId) noexcept = 0;
    // Alive, its graph running (kProcessMe), no player's summon: it can be lent for a pair's half.
    [[nodiscard]] virtual bool ActorCanPair(uint32_t aFormId) const noexcept = 0;

    // --- Actor lifecycle flags ---
    virtual bool IsActorTemporary(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorMount(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDragon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorVampireLord(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDisabled(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDeleted(uint32_t aFormId) const noexcept = 0;
    virtual void EnableActor(uint32_t aFormId) noexcept = 0;
    // At once, no fade; aDelete also marks it deleted.
    virtual void DisableActor(uint32_t aFormId, bool aDelete) noexcept = 0;
    // A held copy with no owner: hidden, it is disabled and every enable refused; not hidden, enables pass (none made).
    virtual void SetActorHidden(uint32_t aFormId, bool aHidden) noexcept = 0;

    // --- Dialogue / sound ---
    virtual void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept = 0;
    virtual void ActorStopCurrentDialogue(uint32_t aFormId) noexcept = 0;
    // The owner's subtitle over the copy (aTopicInfoFormId 0 if this game lacks it); aForce: at any distance.
    virtual void ActorShowSubtitle(uint32_t aFormId, const std::string& acText, uint32_t aTopicInfoFormId, bool aForce) noexcept = 0;

    // --- Player actor properties ---
    virtual void SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept = 0;
    // Left out of this game's player's stealth eye (another player's copy), as MQ201 leaves its allies.
    virtual void SetActorNotOnStealthMeter(uint32_t aFormId, bool aNotShown) noexcept = 0;
    virtual void SetActorPlayerFaction(uint32_t aFormId) noexcept = 0;
    virtual void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept = 0;
    virtual uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept = 0;

    // --- AI / packages ---

    // --- AI state ---
    // One of this game's own actors, as its AI runs it.
    virtual ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept = 0;
    // A copy: holds its owner's AI state.
    /// This game's actor takes a trap's hit the host's script gave its copy there.
    virtual void TakeTrapHit(uint32_t aFormId, uint32_t aTrapFormId, const Payloads::TrapHit& acHit) noexcept = 0;
    virtual void StaggerActor(uint32_t aFormId, float aMagnitude, uint32_t aHitterId) noexcept = 0;
    [[nodiscard]] virtual Perks GetPlayerPerks() const noexcept = 0;
    virtual void SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept = 0;
    virtual void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept = 0;
    // The copies fighting this game's player: it counts as in combat from them.
    virtual void SetCopiesFightingPlayer(uint32_t aCount) noexcept = 0;

    // --- Animation variables ---
    virtual void ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept = 0;
    virtual void ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept = 0;
    // The actor's behaviour graph state as a savegame keeps it, and loading it into a copy.
    virtual std::string SaveActorGraphState(uint32_t aFormId) const noexcept = 0;
    virtual bool LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept = 0;
    virtual ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept = 0;

    // --- Visuals ---
    virtual void ActorFixVampireLordModel(uint32_t aFormId) noexcept = 0;

    // --- Mounting ---
    // A copy rider on its mount at once (Actor::PutOnMount): copies run no mount package.
    virtual void ActorPutOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept = 0;
    // A copy rider whose owner's ride ended: one still seated (its owner got off with no exit clip) is off at once; one
    // getting off already (sit state 4, its owner's HorseExit) ends by its own clip.
    virtual void ActorEndRide(uint32_t aRiderFormId) noexcept = 0;
    // A copy passenger: linked and seated at once (PutOnMount, sit state 3, OnInteractionSeated 41533); its replayed
    // get-on never ends, no horse half playing it.
    virtual void ActorSeatOnMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept = 0;
    // The mount this rider is linked to (its ExtraInteraction), else 0; 0 for a mount, whose slot holds its rider's link.
    [[nodiscard]] virtual uint32_t GetActorMount(uint32_t aRiderFormId) const noexcept = 0;
    // The horse names this rider when it names none: any rider's Detach (41539) empties its one slot.
    virtual void ActorHoldMount(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept = 0;
    // ActorState1::sitSleepState: 3 seated, 4 wants to stand (a rider's exit clip).
    [[nodiscard]] virtual uint8_t GetActorSitState(uint32_t aFormId) const noexcept = 0;
    // Off its mount now, as its get-off clip's end does (FinishDismount 38696), that clip left to end on its own body.
    virtual void ActorFinishDismount(uint32_t aRiderFormId) noexcept = 0;
    // A mount out of a rider's exit pair it was left in: PairEnd, as the pair's instance sends (32797).
    virtual void ActorEndMountPair(uint32_t aMountFormId) noexcept = 0;

    // --- World position ---
    virtual uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept = 0;
    // Its parent cell is attached here (in this game's loaded grid; TESObjectCELL state 7).
    virtual bool IsActorCellAttached(uint32_t aFormId) const noexcept = 0;

    // --- Spawning / NPC serialization ---
    /// Create an Actor from an existing NPC base form. Returns the new actor's formID (0 on failure).
    virtual uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept = 0;
    /// Create a new temporary NPC + Actor from serialized appearance data. Returns new formID.
    /// acAppearance is a raw byte buffer stored as std::string (same type as AppearanceBuffer in messages).
    virtual uint32_t CreateCustomActor(const std::string& acAppearance,
                                        uint32_t aChangeFlags, float aWeight) noexcept = 0;
    // On the main thread between frames: loads a copy's base and rebuilds its 3D. False while a save/load holds FormState.
    virtual bool ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept = 0;
    /// Serialize the actor's NPC base form into a std::string buffer. Returns change flags.
    virtual uint32_t SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept = 0;
    /// Mark NPC changed flags (for appearance marking).
    virtual void MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept = 0;
    /// Get the template base NPC form ID for this actor (for temp NPC chains). 0 if not temp.
    virtual uint32_t GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept = 0;
    /// Returns true if the actor's NPC base is a temporary form.
    virtual bool IsActorNpcTemporary(uint32_t aFormId) const noexcept = 0;
    // A leveled reference (ExtraLeveledCreature): its base is a temporary the engine built from a pick.
    virtual bool IsActorLeveled(uint32_t aFormId) const noexcept = 0;
    // Builds a leveled reference's base again from aPickId as the engine's leveled build does (14375); false if not
    // leveled or the pick is no NPC. Never a permanent record as its base: a reset destroys that (leveled-copy-base.md).
    virtual bool RelevelActor(uint32_t aFormId, uint32_t aPickId) noexcept = 0;

    // --- FaceTints ---
    virtual Tints GetActorFaceTints(uint32_t aFormId) const noexcept = 0;
    virtual void SetupActorFaceGen(uint32_t aFormId) noexcept = 0;

    // --- Systems (CharacterService calls that take Actor*) ---
    /// Run interpolation update for a remote actor.
    virtual void ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept = 0;
    /// Run animation update for a remote actor.
    virtual void ActorUpdateAnimation(uint32_t aFormId, uint64_t aTick) noexcept = 0;
    /// Whether the actor's head shows face tints this game painted; a head built again (a new area, TESNPC::BuildHead
    /// 24731) shows its base's FaceTint file instead.
    virtual bool IsActorFacePainted(uint32_t aFormId) const noexcept = 0;
    /// Paints the tints on the actor's head (FaceGenSystem); true if it now shows them.
    virtual bool PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept = 0;

    // --- Effect visuals: what a magic effect shows on its target (burning, frost, a glow) ---
    // Whether that magic effect shows on its target while it runs (a hit shader or hit art, FX Persist).
    virtual bool EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept = 0;
    // Plays only that effect's hit visuals on the actor until stopped; true if they now play (not
    // before the actor has 3D, nor the first time a shader is used in this game: it loads).
    virtual bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept = 0;
    // Plays that effect's hit visuals once on the actor, as its start does when it is not FX Persist (each runs its
    // own pass and ends); false if it has none or the actor has no 3D.
    virtual bool FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
};
