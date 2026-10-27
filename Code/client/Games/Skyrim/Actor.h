#pragma once

#include <Games/ActorUpkeep.h>
#include <Games/Events.h>
#include <TESObjectREFR.h>

#include <Magic/MagicTarget.h>
#include <Forms/TESActorBase.h>
#include <Misc/ActorState.h>
#include <Misc/IPostAnimationChannelUpdateFunctor.h>
#include <Forms/MagicItem.h>
#include <Magic/ActorMagicCaster.h>

#include <Structs/Inventory.h>
#include <Structs/Factions.h>
#include <Structs/ActorValues.h>
#include <Structs/ActionEvent.h>
#include <Structs/MagicEquipment.h>

struct TESNPC;
struct TESRace;
struct ExActor;
struct ExPlayerCharacter;
struct ActorExtension;
struct AIProcess;
struct CombatController;
struct TESIdleForm;
struct SpellItem;
struct BGSPerk;
struct TESShout;

struct Actor : TESObjectREFR
{
    static constexpr FormType Type = FormType::Character;

    // Allocs and calls constructor
    static GamePtr<Actor> New() noexcept;
    // Places a brand new actor in the world
    static GamePtr<Actor> Create(TESNPC* apBaseForm) noexcept;
    static GamePtr<Actor> Spawn(uint32_t aBaseFormId) noexcept;

    virtual void sub_9C();
    virtual void sub_9D();
    virtual void sub_9E();
    virtual void sub_9F();
    virtual void sub_A0();
    virtual void UnequipObject(void* apUnk1, TESBoundObject* apObject, int32_t aUnk2, void* aUnk3);
    virtual void sub_A2();
    virtual void sub_A3();
    virtual void sub_A4();
    virtual void sub_A5();
    virtual void SetWeaponDrawn(bool aDraw);
    virtual void sub_A7();
    virtual void sub_A8();
    virtual void SetPosition(const NiPoint3& acPoint, bool aSyncHavok = true);
    virtual void sub_AA();
    virtual void Resurrect(bool aResetInventory);
    virtual void sub_AC();
    virtual void sub_AD();
    virtual void sub_AE();
    virtual void sub_AF();
    virtual void sub_B0();
    virtual void sub_B1();
    virtual void sub_B2();
    virtual void sub_B3();
    virtual void sub_B4();
    virtual void sub_B5();
    virtual void sub_B6();
    virtual void sub_B7();
    virtual void sub_B8();
    virtual void sub_B9();
    virtual void sub_BA();
    virtual void PayFine(TESFaction* apFaction, bool aGoToJail, bool aRemoveStolenItems);
    virtual void sub_BC();
    virtual void sub_BD();
    virtual void sub_BE();
    virtual void sub_BF();
    virtual void sub_C0();
    virtual void sub_C1();
    virtual void sub_C2();
    virtual void SetRefraction(bool aEnable, float aRefraction);
    virtual void sub_C4();
    virtual void sub_C5();
    virtual void sub_C6();
    virtual void sub_C7();
    virtual void sub_C8();
    virtual void sub_C9();
    virtual void sub_CA();
    virtual void sub_CB();
    virtual void sub_CC();
    virtual void AttachArrow();
    virtual void sub_CE();
    virtual bool AddShout(TESShout* apShout);         // CF (CommonLibSSE-NG): a player's, onto its base's spell list
    virtual void UnlockWord(TESForm* apWordOfPower); // D0: PlayerCharacter's 40630 sets the word's unlocked flag
    virtual void sub_D1();
    virtual bool sub_D2() const;
    virtual void sub_D3();
    virtual void sub_D4();
    virtual void sub_D5();
    virtual void sub_D6();
    virtual void sub_D7();
    virtual void sub_D8();
    virtual void sub_D9();
    virtual void sub_DA();
    virtual void sub_DB();
    virtual void sub_DC();
    virtual void sub_DD();
    virtual void sub_DE();
    virtual void PutCreatedPackage(struct TESPackage*, bool = false, bool = true, bool = true); // 14069BBF0
    virtual void UpdateAlpha();
    virtual void sub_E1();
    virtual void sub_E2();
    virtual void sub_E3();
    virtual void sub_E4();
    virtual void sub_E5();
    virtual void sub_E6();
    virtual void sub_E7();
    virtual void sub_E8();
    virtual void sub_E9();
    virtual void sub_EA();
    virtual void sub_EB();
    virtual void sub_EC();
    virtual void sub_ED();
    virtual void sub_EE();
    virtual void sub_EF();
    // these 4 are not virtual funcs in fallout 4
    virtual bool MoveToHigh();
    virtual bool MoveToLow();
    virtual bool MoveToMiddleLow();
    virtual bool MoveToMiddleHigh();
    virtual bool sub_F4();
    virtual void sub_F5();
    virtual void sub_F6();
    virtual void sub_F7();
    virtual void sub_F8();
    virtual void sub_F9();
    virtual void sub_FA();
    // CommonLibSSE-NG AddPerk/RemovePerk (0FB/0FC); the player's: PlayerCharacter 40681 (rank 0: one more), 40682 (queued).
    virtual void AddPerk(BGSPerk* apPerk, uint32_t aRank);
    virtual void RemovePerk(BGSPerk* apPerk);
    virtual void sub_FD();
    virtual void sub_FE();
    virtual void sub_FF();
    virtual void sub_100();
    virtual void ApplyPerksFromBase(); // CommonLibSSE-NG 0x101: its perks' abilities cast again
    virtual void sub_102();
    virtual void sub_103();
    virtual void HandleHealthDamage(Actor* apAttacker, float aDamage); // 38606
    virtual void sub_105();
    virtual void sub_106();
    virtual void sub_107();
    virtual void sub_108();
    virtual void InitializeMovementControllerNPC();
    virtual void sub_10A();
    virtual void sub_10B();
    virtual void sub_10C();
    virtual void sub_10D();
    virtual void KillImpl(Actor* apAttacker, float aDamage, bool aSendEvent, bool aRagdollInstant);
    virtual void DrinkPotion(TESBoundObject* apItem, ExtraDataList* apExtraDataList); // 90% sure about this
    virtual void sub_110();
    virtual void sub_111();
    virtual void sub_112();
    virtual void sub_113();
    virtual void sub_114();
    virtual bool sub_115();
    virtual bool sub_116();
    virtual void sub_117();
    virtual void sub_118();
    virtual void sub_119();
    virtual void sub_11A();
    virtual void sub_11B();
    virtual void sub_11C();
    virtual void sub_11D();
    virtual void sub_11E();
    virtual void sub_11F();
    virtual void sub_120();
    virtual void sub_121();
    virtual void sub_122();
    virtual void sub_123();
    virtual void sub_124();
    virtual void sub_125();
    virtual void sub_126();
    virtual void sub_127();

    // Real functions
    void DualCastSpell(TESObjectREFR* apDesiredTarget) noexcept;
    void InterruptCast(bool abRefund) noexcept;

    // Casting
    ActorExtension* GetExtension() noexcept;
    ExActor* AsExActor() noexcept;
    ExPlayerCharacter* AsExPlayerCharacter() noexcept;

    // Getters
    float GetSpeed() noexcept;
    TESForm* GetEquippedWeapon(uint32_t aSlotId) const noexcept;
    TESForm* GetEquippedAmmo() const noexcept;
    Actor* GetCommandingActor() const noexcept;
    // in reality this is a BGSLocation
    TESForm* GetCurrentLocation();
    float GetActorValue(uint32_t aId) const noexcept;
    float GetActorPermanentValue(uint32_t aId) const noexcept;
    Inventory GetActorInventory() const noexcept;
    MagicEquipment GetMagicEquipment() const noexcept;
    Inventory GetEquipment() const noexcept;
    int32_t GetGoldAmount() const noexcept;
    uint16_t GetLevel() const noexcept;
    Factions GetFactions() const noexcept;
    ActorValues GetEssentialActorValues() const noexcept;
    [[nodiscard]] bool IsDead() const noexcept;
    // It lies in its ragdoll, dead or knocked down (Actor::IsInRagdollState 37491): the engine keeps it on the ragdoll.
    [[nodiscard]] bool LiesInRagdoll() const noexcept;
    [[nodiscard]] bool IsDragon() const noexcept;
    [[nodiscard]] bool IsPlayerSummon() const noexcept;
    [[nodiscard]] bool IsInCombat() const noexcept;
    [[nodiscard]] Actor* GetCombatTarget() const noexcept;
    [[nodiscard]] bool HasPerk(uint32_t aPerkFormId) const noexcept;
    [[nodiscard]] uint8_t GetPerkRank(uint32_t aPerkFormId) const noexcept;
    [[nodiscard]] bool IsWearingBodyPiece() const noexcept;
    [[nodiscard]] bool ShouldWearBodyPiece() const noexcept;
    [[nodiscard]] bool IsVampireLord() const noexcept;

    // Setters
    void SetSpeed(float aSpeed) noexcept;
    void SetLevelMod(uint32_t aLevel) noexcept;
    void SetActorValue(uint32_t aId, float aValue) noexcept;
    void ForceActorValue(ActorValueOwner::ForceMode aMode, uint32_t aId, float aValue) noexcept;
    // Health damage as the engine's hits take it, with the attacker for its death rules and kill credit.
    void DamageHealth(float aDamage, Actor* apAttacker) noexcept;
    void SetActorValues(const ActorValues& acActorValues) noexcept;
    void SetCommandingActor(BSPointerHandle<TESObjectREFR> aCommandingActor) noexcept;
    void SetFactions(const Factions& acFactions) noexcept;
    void SetFactionRank(const TESFaction* apFaction, int8_t aRank) noexcept;
    void ForcePosition(const NiPoint3& acPosition) noexcept;
    void SetWeaponDrawnEx(bool aDraw) noexcept;
    void InitiateDoNothingPackage() noexcept;
    // The package conditions and Papyrus see (37399): the running one, or for an engine-made one the form package
    // under it (ExtraPackage).
    [[nodiscard]] TESPackage* GetCurrentPackage() const noexcept;
    // A copy: holds its owner's package in its running slot, without a target; UpdateAI, which runs it, is cut.
    void PutOwnerPackage(TESPackage* apPackage) noexcept;
    // The scene it plays a part in, as this game runs it (Actor::GetCurrentScene 37247, not a copy's owner's).
    [[nodiscard]] struct BGSScene* GetOwnCurrentScene() const noexcept;
    // Its combat state as this game runs it (Actor::GetCombatState 38556: 0 none, 1 fighting, 2 searching), not a
    // copy's owner's.
    [[nodiscard]] uint8_t GetOwnCombatState() const noexcept;
    void SetActorInventory(const Inventory& aInventory) noexcept;
    void SetMagicEquipment(const MagicEquipment& acEquipment) noexcept;
    // Its own flag and its base's.
    void SetEssentialEx(bool aSet) noexcept;
    // A player's copy: in the player's faction (0xDB1), as the player's allies take the player.
    void JoinPlayerFaction() noexcept;
    // Its abilities and worn enchantments cast again (CommonLibSSE-NG CastPermanentMagic, 38753).
    void CastPermanentMagic(bool aWornItemEnchantments, bool aBaseSpells, bool aRaceSpells, bool aEveryActorAbility) noexcept;
    // Its 3D rebuilt from its reference (CommonLibSSE-NG DoReset3D, 40255).
    void DoReset3D(bool aUpdateWeight) noexcept;
    void SetPlayerTeammate(bool aSet) noexcept;

    // Actions
    void UnEquipAll() noexcept;
    void RemoveFromAllFactions() noexcept;
    void QueueUpdate() noexcept;
    bool InitiateMountPackage(Actor* apMount) noexcept;
    // On its mount at once, with no package or animation, as InitiateMountPackage links a low-process rider. For a
    // copy, which runs no AI: its owner's actions play the rest, the replayed getting off unlinking it too.
    bool PutOnMount(Actor* apMount) noexcept;
    void GenerateMagicCasters() noexcept;
    void DispelAllSpells(bool aNow = false) noexcept;
    void Reset() noexcept;
    void Kill() noexcept;
    // The engine keeps the actor alive whatever its health until EndDeferredKill (papyrus StartDeferredKill).
    void StartDeferredKill() noexcept;
    void EndDeferredKill() noexcept;
    // A kill move's victim: the kill move kills it (KillActorHandler 42838, crediting its partner).
    [[nodiscard]] bool IsHeldByKillMove() const noexcept;
    [[nodiscard]] bool IsInKillMove() const noexcept;
    // A copy's kill as its owner's went: KillImpl performs the owner's death action (true if taken).
    bool KillWithDeathAction(const ActionEvent& acAction) noexcept;
    void Respawn() noexcept;
    void PickUpObject(TESObjectREFR* apObject, int32_t aCount, bool aUnk1, float aUnk2) noexcept;
    void DropObject(TESBoundObject* apObject, ExtraDataList* apExtraData, int32_t aCount, NiPoint3* apLocation, NiPoint3* apRotation) noexcept;
    void DropOrPickUpObject(const Inventory::Entry& arEntry, NiPoint3* apPoint, NiPoint3* apRotate) noexcept;
    void SpeakSound(const char* pFile);
    void StartCombatEx(Actor* apTarget) noexcept;
    void SetCombatTargetEx(Actor* apTarget) noexcept;
    void StartCombat(Actor* apTarget) noexcept;
    void StopCombat() noexcept;
    bool PlayIdle(TESIdleForm* apIdle) noexcept;
    void FixVampireLordModel() noexcept;
    bool RemoveSpell(MagicItem* apSpell) noexcept;
    // Actor::AddSpell (38716): false when it already has it.
    bool AddSpell(SpellItem* apSpell) noexcept;
    // Actor::SwitchRace (37925): its race and 3D, as the engine's race change does.
    void SwitchRace(TESRace* apRace, bool aPlayer) noexcept;

    enum ActorFlags
    {
        IS_A_MOUNT = 1 << 1,
        GETTING_ON_OFF_MOUNT = 1 << 3, // from InitiateMountPackage or Dismount until MountDismountEndHandler (42881)
        SHOULD_ANIM_GRAPH_UPDATE = 1 << 8, // its graph runs (holder vfunc 0x0E, 37985); KillImpl clears it
        CAN_SPEAK = 1 << 13,               // cleared for the dead (Character::UpdateAI 37347)
        IS_COMMANDED_ACTOR = 1 << 16,
        IS_ESSENTIAL = 1 << 18,
    };

    bool IsMount() const noexcept { return flags2 & ActorFlags::IS_A_MOUNT; }

    bool IsEssential() const noexcept { return flags2 & ActorFlags::IS_ESSENTIAL; }
    void SetEssential(bool aSetEssential) noexcept
    {
        if (aSetEssential)
            flags2 |= ActorFlags::IS_ESSENTIAL;
        else
            flags2 &= ~ActorFlags::IS_ESSENTIAL;
    }

    bool IsCommandedActor() const noexcept { return flags2 & ActorFlags::IS_COMMANDED_ACTOR; }

public:
    enum ChangeFlags : uint32_t
    {
        CHANGE_ACTOR_LIFESTATE = 1 << 10,
        CHANGE_ACTOR_EXTRA_PACKAGE_DATA = 1 << 11,
        CHANGE_ACTOR_EXTRA_MERCHANT_CONTAINER = 1 << 12,
        CHANGE_ACTOR_EXTRA_DISMEMBERED_LIMBS = 1 << 17,
        CHANGE_ACTOR_LEVELED_ACTOR = 1 << 18,
        CHANGE_ACTOR_DISPOSITION_MODIFIERS = 1 << 19,
        CHANGE_ACTOR_TEMP_MODIFIERS = 1 << 20,
        CHANGE_ACTOR_DAMAGE_MODIFIERS = 1 << 21,
        CHANGE_ACTOR_OVERRIDE_MODIFIERS = 1 << 22,
        CHANGE_ACTOR_PERMANENT_MODIFIERS = 1 << 23,
    };

    struct ActorValueModifiers
    {
        float permanentModifier;
        float temporaryModifier;
        float damageModifier;
    };

    struct SpellItemEntry
    {
        SpellItem* pItem;
        float unk8;
        SpellItemEntry* pNext;
    };

    MagicTarget magicTarget;
    ActorValueOwner actorValueOwner;
    ActorState actorState;
    BSTEventSink<void*> tdEvent;
    BSTEventSink<void*> cmfEvent;
    IPostAnimationChannelUpdateFunctor unk78;
    uint32_t flags1; // CommonLib boolBits
    // flags1: the engine updates the actor this frame (CommonLib BOOL_BITS::kProcessMe).
    static constexpr uint32_t kProcessMe = 1u << 1;
    float headTrackingUpdateDelay;
    uint32_t unk84;
    AIProcess* currentProcess;
    uint32_t dialogueHandle;
    uint32_t combatHandle;
    uint32_t killerHandle;
    uint32_t unk98;
    float fVoiceTimer; // seconds left of the voice line (CommonLib voiceTimer)
    float fUnderWaterTimer; // seconds the head has been under water (CommonLib underWaterTimer)
    uint32_t unkA4;
    int32_t iThiefCrimeStamp;
    int32_t iActionValue;
    float fTimerOnAction;
    uint32_t unkB4[5];
    void* pad138[2];
    struct IMovementDrivenControl* movementDrivenControl; // MovementControllerNPC
    uint32_t unk0D4;
    CombatController* pCombatController;
    void* unkDC;
    uint32_t unkE0;
    uint32_t unkE4;
    uint32_t unkE8;
    uint32_t unkEC;
    uint32_t unk178;               // F0
    uint32_t unk17C;               // F4
    SpellItemEntry* spellItemHead; // F8
    BSTSmallArray<SpellItem> addedSpells; // spells added to this actor (AddSpell)
    ActorMagicCaster* casters[4];
    MagicItem* magicItems[4];
    TESForm* equippedShout;
    uint32_t someRefrHandle;
    TESRace* race;
    float weight; // set to -1.0 by extra container changes data ctor, asuming it's the weight
    uint32_t flags2; // CommonLib boolFlags
    // flags2: run the animation graph update even while the actor is off-screen (CommonLib
    // BOOL_FLAGS::kForceAnimGraphUpdate; read by Actor::ModifyAnimationUpdateData, 38054).
    static constexpr uint32_t kForceAnimGraphUpdate = 1u << 29;
    // flags2: the head is under water (CommonLib BOOL_FLAGS::kUnderwater; set by Actor::Update, 37348).
    static constexpr uint32_t kUnderwater = 1u << 31;
    void* unk200[4];
    struct BGSDialogueBranch* dialogueBranch;
    ActorValueModifiers healthModifiers;
    ActorValueModifiers staminaModifiers;
    ActorValueModifiers magickaModifiers;
    ActorValueModifiers voiceModifiers;
    float fLastUpdate;
    uint32_t iLastSeenTime;
    void* actorWeightData;
    float fArmorRating;
    float fArmorBaseFactorSum;
    // Voice line state (CommonLib soundCallBackSet): 3 queued, -1 playing, 1/2 ended.
    int8_t soundCallBackSet;
    uint8_t unk195;
    // Character vfunc 0xC5 (40292) reads it, 0xC4 (40295) sets it: AIProcess::KnockExplosion (39895) knocks the
    // actor into its ragdoll only with it, else pushes its controller.
    uint8_t knockRagdolls;
    uint8_t flag197;

    uint8_t pad198[8];
    BSRecursiveLock actorLock;
    uint8_t padActorEnd[0x2B0 - 0x284];

};

static_assert(offsetof(Actor, currentProcess) == 0xF8);
static_assert(offsetof(Actor, tdEvent) == 0xD0); // BSTEventSink<BSTransformDeltaEvent> (CommonLib-NG)
static_assert(offsetof(Actor, combatHandle) == 0x104); // currentCombatTarget, what GetCombatTarget reads (54680, 22318)
static_assert(offsetof(Actor, flags1) == 0xE8);
static_assert(offsetof(Actor, actorValueOwner) == 0xB8);
static_assert(offsetof(Actor, actorState) == 0xC0);
static_assert(offsetof(Actor, flags2) == 0x204);
static_assert(offsetof(Actor, soundCallBackSet) == 0x278);
static_assert(offsetof(Actor, knockRagdolls) == 0x27A);
static_assert(offsetof(Actor, fVoiceTimer) == 0x110);
static_assert(offsetof(Actor, fUnderWaterTimer) == 0x114);
static_assert(offsetof(Actor, unk84) == 0xF0);
static_assert(offsetof(Actor, unk17C) == 0x184);
static_assert(offsetof(Actor, pCombatController) == 0x160);
static_assert(offsetof(Actor, magicItems) == 0x1C8);
static_assert(offsetof(Actor, equippedShout) == 0x1E8);
static_assert(offsetof(Actor, addedSpells) == 0x190);
static_assert(offsetof(Actor, race) == 0x1F8);
static_assert(offsetof(Actor, actorLock) == 0x284);
static_assert(sizeof(Actor) == 0x2B8);
static_assert(sizeof(Actor::SpellItemEntry) == 0x18);

// The engine's "angry with the player" exists only for its own player. For a remote player, an
// actor pushed past its friendly-hit tolerance is marked angry with that player's character here,
// and Actor::IsHostileToActor reports it hostile (see HookIsHostileToActor).
void MarkAngryWithRemotePlayer(Actor* apActor, Actor* apRemotePlayer) noexcept;
