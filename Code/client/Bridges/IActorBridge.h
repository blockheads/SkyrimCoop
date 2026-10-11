#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <Structs/ActorValues.h>
#include <Structs/AnimationVariables.h>
#include <Structs/Inventory.h>
#include <Structs/Faction.h>
#include <Structs/Factions.h>
#include <Structs/Tints.h>
#include <Structs/ActionEvent.h>
#include <Structs/WeaponState.h>

/// All Actor* and related operations abstracted away from SKSE headers.
/// Implementations must NOT leak any SKSE type into the return type or
/// parameter list — only primitives, std::string, std::vector<T>, and
/// Structs/ types (which have no SKSE dependency).
class IActorBridge
{
public:
    virtual ~IActorBridge() = default;

    // --- Existence / identity ---
    virtual bool ActorExists(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDead(uint32_t aFormId) const noexcept = 0;
    virtual bool IsPlayerSummon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsTemporaryActor(uint32_t aFormId) const noexcept = 0;

    // --- Remote / player flags (ActorExtension) ---
    virtual bool IsRemotePlayer(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorRemote(uint32_t aFormId, bool aRemote) noexcept = 0;
    /// True for replicas (driven by the network), false for actors this game simulates.
    virtual bool IsActorRemote(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept = 0;

    // --- Ownership / command ---
    virtual uint32_t GetCommandingActor(uint32_t aFormId) const noexcept = 0;

    // --- State ---
    virtual void KillActor(uint32_t aFormId) noexcept = 0;
    // Kill with a killer (0 = none), so death events and kill credit name them.
    virtual void KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept = 0;
    virtual void RespawnActor(uint32_t aFormId) noexcept = 0;
    // Brings a dead actor back where it is (the console's `resurrect`), without a Reset.
    virtual void ResurrectActor(uint32_t aFormId) noexcept = 0;
    virtual void DeleteActor(uint32_t aFormId) noexcept = 0;

    // --- Position / movement ---
    virtual void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept = 0;
    virtual void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept = 0;
    // Resident game-thread operation; no pointers into a reloadable DLL retained.
    virtual bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
        const float aRotation[3], float aDirection, const AnimationVariables& aVariables) noexcept = 0;
    virtual void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;
    virtual void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;

    // --- Actor values ---
    virtual float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept = 0;
    virtual void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept = 0;
    virtual int32_t GetActorValueCount() const noexcept = 0;

    // --- Equipment / weapon ---
    virtual void EquipItem(uint32_t aActorFormId, uint32_t aItemFormId, bool aUnequipSlot) noexcept = 0;
    virtual void UnequipItem(uint32_t aActorFormId, uint32_t aItemFormId) noexcept = 0;
    virtual void DrawWeapon(uint32_t aFormId, bool aDraw) noexcept = 0;
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
    virtual void AddMagicTarget(uint32_t aActorFormId, uint32_t aCasterFormId, uint32_t aTargetFormId,
                                 bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept = 0;

    // --- Inventory ---
    virtual bool IsBleedingOut(uint32_t aFormId) const noexcept = 0;
    virtual void PayCrimeGoldToAllFactions(uint32_t aFormId) noexcept = 0;
    virtual uint32_t GetEquippedShoutFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActiveMagicItemFormId(uint32_t aFormId, uint32_t aSlot) const noexcept = 0;

    // --- Combat / target ---
    virtual uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept = 0;

    // --- Base form ---
    virtual uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetActorBaseName(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept = 0;

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
    virtual ActorValues GetActorEssentialValues(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept = 0;

    // --- Inventory (bulk) ---
    virtual Inventory GetActorInventory(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept = 0;

    // --- Factions ---
    virtual Factions GetActorFactions(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept = 0;

    // --- Weapon draw ---
    virtual void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept = 0;
    virtual WeaponState GetWeaponState(uint32_t aFormId) const noexcept = 0;

    // --- Actor lifecycle flags ---
    virtual bool IsActorTemporary(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorMount(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDragon(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorVampireLord(uint32_t aFormId) const noexcept = 0;
    virtual bool IsActorDisabled(uint32_t aFormId) const noexcept = 0;
    virtual void EnableActor(uint32_t aFormId) noexcept = 0;

    // --- Dialogue / sound ---
    virtual void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept = 0;
    virtual void ActorStopCurrentDialogue(uint32_t aFormId) noexcept = 0;

    // --- Player actor properties ---
    virtual void SetActorIgnoreFriendlyHit(uint32_t aFormId, bool aIgnore) noexcept = 0;
    virtual void SetActorPlayerRespawnMode(uint32_t aFormId) noexcept = 0;
    virtual void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept = 0;
    virtual uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept = 0;

    // --- AI / packages ---
    virtual void SetActorPackage(uint32_t aFormId, uint32_t aPackageFormId) noexcept = 0;

    // --- Animation variables ---
    virtual void ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept = 0;
    virtual void ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept = 0;
    virtual ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept = 0;

    // --- Visuals ---
    virtual void ActorFixVampireLordModel(uint32_t aFormId) noexcept = 0;

    // --- Mounting ---
    virtual void ActorInitiateMountPackage(uint32_t aRiderFormId, uint32_t aMountFormId) noexcept = 0;

    // --- World position ---
    virtual uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept = 0;

    // --- Spawning / NPC serialization ---
    /// Create an Actor from an existing NPC base form. Returns the new actor's formID (0 on failure).
    virtual uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept = 0;
    /// Create a new temporary NPC + Actor from serialized appearance data. Returns new formID.
    /// acAppearance is a raw byte buffer stored as String (same type as AppearanceBuffer in messages).
    virtual uint32_t CreateCustomActor(const TiltedPhoques::String& acAppearance,
                                        uint32_t aChangeFlags) noexcept = 0;
    /// Deserialize appearance data onto an existing actor's NPC base form.
    /// acAppearance is a raw byte buffer stored as String.
    virtual void DeserializeActorNpc(uint32_t aFormId, const TiltedPhoques::String& acAppearance,
                                      uint32_t aChangeFlags) noexcept = 0;
    /// Serialize the actor's NPC base form into a String buffer. Returns change flags.
    virtual uint32_t SerializeActorNpc(uint32_t aFormId, TiltedPhoques::String& aBuffer) noexcept = 0;
    /// Mark NPC changed flags (for appearance marking).
    virtual void MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept = 0;
    /// Get the template base NPC form ID for this actor (for temp NPC chains). 0 if not temp.
    virtual uint32_t GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept = 0;
    /// Returns true if the actor's NPC base is a temporary form.
    virtual bool IsActorNpcTemporary(uint32_t aFormId) const noexcept = 0;

    // --- FaceTints ---
    virtual Tints GetActorFaceTints(uint32_t aFormId) const noexcept = 0;
    virtual void SetupActorFaceGen(uint32_t aFormId) noexcept = 0;

    // --- CacheSystem integration ---
    virtual void SetupActorCache(uint32_t aFormId, uint32_t aEntityHint) noexcept = 0;

    // --- Systems (CharacterService calls that take Actor*) ---
    /// Run interpolation update for a remote actor.
    virtual void ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept = 0;
    /// Run animation update for a remote actor.
    virtual void ActorUpdateAnimation(uint32_t aFormId, uint64_t aTick) noexcept = 0;
    /// Run facegen update for a remote actor.
    virtual void ActorUpdateFaceGen(uint32_t aFormId) noexcept = 0;

    // --- Reveal effect (SkyrimTogether.esp reveal) ---
    /// Apply the reveal detect effect to a remote player actor.
    virtual void ApplyRevealEffect(uint32_t aFormId, float aMagnitude) noexcept = 0;

    // --- Player-specific (knockdown) ---
    virtual void PlayerKnockExplosion(uint32_t aFormId, float aX, float aY, float aZ, float aForce) noexcept = 0;

    // --- Effect visuals: what a magic effect shows on its target (burning, frost, a glow) ---
    // Whether that magic effect shows on its target (a hit shader or hit art).
    virtual bool EffectHasHitVisuals(uint32_t aEffectFormId) const noexcept = 0;
    // Plays only that effect's hit visuals on the actor until stopped; true if they now play (not
    // before the actor has 3D, nor the first time a shader is used in this game: it loads).
    virtual bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept = 0;
    virtual bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept = 0;
    // The effect shaders and art objects the engine is playing on the actor (test harness).
    virtual std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) const noexcept = 0;
};
