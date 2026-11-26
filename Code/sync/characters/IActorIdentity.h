#pragma once

#include <Structs/Tints.h>
#include <string>
#include <vector>
#include <cstdint>

// Who an actor is and where it stands in the world: its flags, base, looks, life and mount. One part of IActorBridge (ports/IActorBridge.h).
struct IActorIdentity
{
    virtual ~IActorIdentity() = default;

    virtual bool ActorExists(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorStillLoading(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDead(uint32_t aFormId) const noexcept = 0;
    virtual bool IsPlayerSummon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsTemporaryActor(uint32_t aFormId) const noexcept = 0;
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
    virtual uint32_t GetCommandingActor(uint32_t aFormId) const noexcept = 0;
    virtual void KillActor(uint32_t aFormId) noexcept = 0;
    virtual void RespawnActor(uint32_t aFormId) noexcept = 0;
    // Brings a dead actor back where it is (the console's `resurrect`), without a Reset.
    virtual void ResurrectActor(uint32_t aFormId) noexcept = 0;
    // Death stops graph updates and speech; resume them after a confirmed resurrection.
    virtual void ResumeActorAnimation(uint32_t aFormId) noexcept = 0;
    virtual void DeleteActor(uint32_t aFormId) noexcept = 0;
    virtual bool IsBleedingOut(uint32_t aFormId) const noexcept = 0;
    virtual void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept = 0;
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
    virtual bool ActorHas3D(uint32_t aFormId) const noexcept = 0;
    /// Actors in the engine's high process list with loaded 3D, excluding the player.
    virtual std::vector<uint32_t> GetLoadedActorFormIds() const noexcept = 0;
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
    virtual void SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept = 0;
    // Left out of this game's player's stealth eye (another player's copy), as MQ201 leaves its allies.
    virtual void SetActorNotOnStealthMeter(uint32_t aFormId, bool aNotShown) noexcept = 0;
    virtual void SetActorPlayerFaction(uint32_t aFormId) noexcept = 0;
    virtual void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept = 0;
    virtual uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept = 0;
    virtual void ActorFixVampireLordModel(uint32_t aFormId) noexcept = 0;
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
    virtual Tints GetActorFaceTints(uint32_t aFormId) const noexcept = 0;
    virtual void SetupActorFaceGen(uint32_t aFormId) noexcept = 0;
    /// Whether the actor's head shows face tints this game painted; a head built again (a new area, TESNPC::BuildHead
    /// 24731) shows its base's FaceTint file instead.
    virtual bool IsActorFacePainted(uint32_t aFormId) const noexcept = 0;
    /// Paints the tints on the actor's head (FaceGenSystem); true if it now shows them.
    virtual bool PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept = 0;
};
