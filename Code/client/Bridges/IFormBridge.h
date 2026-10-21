#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Structs/Inventory.h>
#include <Structs/LockData.h>

/// TESForm lookups and typed Cast<> operations.
/// No SKSE types in any signature — all IDs use uint32_t.
class IFormBridge
{
public:
    /// Flat mod descriptor returned by GetLoadedMods().
    struct ModEntry
    {
        uint16_t    Id{};       ///< Effective mod ID (lite or standard)
        bool        IsLite{};   ///< True if the mod is an ESL/lite plugin
        std::string Filename;   ///< Plugin filename (e.g. "Skyrim.esm")
    };

    virtual ~IFormBridge() = default;

    // --- Generic form queries ---
    virtual bool FormExists(uint32_t aFormId) const noexcept = 0;
    virtual uint8_t GetFormType(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetFormEditorId(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetFormName(uint32_t aFormId) const noexcept = 0;

    // --- TESGlobal ---
    virtual float GetGlobalValue(uint32_t aFormId) const noexcept = 0;
    virtual void SetGlobalValue(uint32_t aFormId, float aValue) noexcept = 0;

    // --- Cell / world ---
    virtual uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept = 0;
    virtual uint32_t LookupWorldSpaceByFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetParentCellFormId(uint32_t aObjectFormId) const noexcept = 0;
    virtual uint32_t GetParentWorldSpaceFormId(uint32_t aObjectFormId) const noexcept = 0;

    // --- Quest ---
    virtual bool QuestExists(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetQuestStage(uint32_t aFormId) const noexcept = 0;
    virtual bool IsQuestStopped(uint32_t aFormId) const noexcept = 0;
    virtual uint8_t GetQuestType(uint32_t aFormId) const noexcept = 0;
    virtual std::string GetQuestName(uint32_t aFormId) const noexcept = 0;
    virtual void SetQuestActive(uint32_t aFormId, bool aActive) noexcept = 0;
    virtual uint32_t FindQuestByNameId(const std::string& aNameId) const noexcept = 0;
    /// Programmatically set quest stage (equivalent to TESQuest::ScriptSetStage).
    virtual void ScriptSetQuestStage(uint32_t aFormId, uint32_t aStage) noexcept = 0;
    /// Force-stop a quest (equivalent to TESQuest::SetStopped).
    virtual void SetQuestStopped(uint32_t aFormId) noexcept = 0;
    /// Returns true if the quest has no stage entries (non-syncable quest check).
    virtual bool IsQuestStagesEmpty(uint32_t aFormId) const noexcept = 0;
    /// Returns true if a ScopedQuestOverride is currently active.
    /// Used by QuestService to suppress duplicate quest events during remote updates.
    virtual bool IsQuestOverrideActive() const noexcept = 0;

    // --- Mod registry ---
    virtual uint32_t GetModCount() const noexcept = 0;
    virtual std::string GetModNameByIndex(uint32_t aIndex) const noexcept = 0;
    virtual uint32_t GetModStandardId(const std::string& aModName) const noexcept = 0;
    virtual std::vector<std::string> GetAllModNames() const noexcept = 0;
    virtual uint32_t GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept = 0;
    /// Returns a flat list of all currently loaded mods (IsLoaded() == true).
    /// Used by TransportService to build the authentication mod list without touching ModManager directly.
    virtual std::vector<ModEntry> GetLoadedMods() const noexcept = 0;

    // --- Typed lookup helpers ---
    virtual bool IsSpellItem(uint32_t aFormId) const noexcept = 0;
    virtual bool IsConcentrationSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsHealingSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsBuffSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsWardSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsInvisibilitySpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsBoundWeaponSpell(uint32_t aFormId) const noexcept = 0;
    virtual bool IsMagicItem(uint32_t aFormId) const noexcept = 0;
    /// Returns the casting type enum value for a spell (0=Fire, 1=Duration, 2=Concentration, 3=Constant).
    virtual uint32_t GetSpellCastingType(uint32_t aFormId) const noexcept = 0;
    /// Returns true if the given effect item (by effectFormId within spellFormId) is the werewolf effect.
    virtual bool IsSpellEffectWerewolf(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept = 0;
    /// Returns true if the given effect item is the vampire lord effect.
    virtual bool IsSpellEffectVampireLord(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept = 0;
    /// Returns true if the given effect item is a slow-time effect.
    virtual bool IsSpellEffectSlow(uint32_t aSpellFormId, uint32_t aEffectFormId) const noexcept = 0;
    /// Apply a magic target effect on an actor using a specific effect from a spell.
    virtual void AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId,
                                       uint32_t aSpellFormId, uint32_t aEffectFormId,
                                       float aMagnitude, bool aIsDualCasting,
                                       bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept = 0;

    // --- Object (TESObjectREFR) operations ---
    /// Returns true if the object has a lock component.
    virtual bool ObjectHasLock(uint32_t aObjectFormId) const noexcept = 0;
    /// Returns true if the object's lock is currently locked.
    virtual bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept = 0;
    /// Returns the lock level of the object (0 = no lock).
    virtual uint8_t GetObjectLockLevel(uint32_t aObjectFormId) const noexcept = 0;
    /// Set the lock state on an object (creates lock if needed).
    virtual bool SetObjectLock(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept = 0;
    /// Notify the game engine that the object's lock state changed (plays sounds, updates 3D).
    virtual void NotifyObjectLockChange(uint32_t aObjectFormId) noexcept = 0;
    /// Returns the form ID of the object's parent cell.
    virtual uint32_t GetObjectParentCellId(uint32_t aObjectFormId) const noexcept = 0;
    /// Activate an object as if the given actor (or reference: a host script's door) activated it.
    virtual void ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aPreActivationState) noexcept = 0;
    /// Play an animation on an object (for ScriptAnimationEvent).
    virtual void PlayObjectAnimation(uint32_t aObjectFormId, const std::string& aAnimation, const std::string& aEventName) noexcept = 0;
    /// Returns the open state of an object (for door sync).
    virtual uint8_t GetObjectOpenState(uint32_t aObjectFormId) const noexcept = 0;
    /// Returns true if the object's base form is of the specified FormType enum value.
    virtual bool IsObjectBaseFormType(uint32_t aObjectFormId, uint8_t aFormType) const noexcept = 0;
    /// A door that leads to another cell.
    virtual bool IsLoadDoor(uint32_t aObjectFormId) const noexcept = 0;
    /// Get the current inventory contents of an object (container).
    virtual Inventory GetObjectInventory(uint32_t aObjectFormId) const noexcept = 0;
    /// Overwrite the inventory of an object, retaining quest items from the current inventory.
    virtual void SetObjectInventoryRetainingQuestItems(uint32_t aObjectFormId,
                                                        const Inventory& aCurrentInventory,
                                                        const Inventory& aNewInventory) noexcept = 0;
    /// Overwrite the inventory of an object entirely.
    virtual void SetObjectInventory(uint32_t aObjectFormId, const Inventory& aInventory) noexcept = 0;
    /// Returns true if the given equip slot form ID is the right-hand equip slot
    /// (DefaultObjectManager::Get().rightEquipSlot).
    virtual bool IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept = 0;

    // --- Projectile launch (for CombatService) ---
    /// Launch a projectile from a remote shooter.
    virtual void LaunchProjectile(uint32_t aShooterFormId, uint32_t aCellFormId,
                                   float aOriginX, float aOriginY, float aOriginZ,
                                   uint32_t aProjectileBaseId, uint32_t aWeaponId, uint32_t aAmmoId,
                                   uint32_t aSpellId, uint32_t aCastingSource,
                                   float aZAngle, float aXAngle, float aYAngle,
                                   int32_t aArea, float aPower, float aScale,
                                   bool aAlwaysHit, bool aNoDamageOutsideCombat, bool aAutoAim,
                                   bool aDeferInit, bool aForceConeOfFire,
                                   bool aUnkBool1, bool aUnkBool2) noexcept = 0;

    // --- Actor inventory / equipment helpers (for InventoryService) ---
    /// Get the full equipment (worn items) of an actor as an Inventory.
    virtual Inventory GetActorEquipment(uint32_t aActorFormId) const noexcept = 0;
    /// Drop or pick up an item from an actor's inventory.
    virtual void ActorDropOrPickUpObject(uint32_t aActorFormId, const Inventory::Entry& aItem) noexcept = 0;
    /// Add or remove an item from an object/actor's inventory.
    virtual void ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept = 0;
    /// Get worn armor inventory of an actor.
    virtual Inventory GetActorWornArmor(uint32_t aActorFormId) const noexcept = 0;
    /// Returns true if the actor is wearing a body piece.
    virtual bool ActorIsWearingBodyPiece(uint32_t aActorFormId) const noexcept = 0;
    /// Returns true if the actor should be wearing a body piece (NPC check).
    virtual bool ActorShouldWearBodyPiece(uint32_t aActorFormId) const noexcept = 0;
    /// Reset an actor's inventory to defaults.
    virtual void ActorResetInventory(uint32_t aActorFormId, bool aBroadcast) noexcept = 0;
    /// Equip a spell on an actor using EquipManager.
    virtual void ActorEquipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept = 0;
    /// Unequip a spell from an actor using EquipManager.
    virtual void ActorUnequipSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aSlotId) noexcept = 0;
    /// Equip a shout on an actor using EquipManager.
    virtual void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept = 0;
    /// Unequip a shout from an actor using EquipManager.
    virtual void ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept = 0;
    /// Full equip via EquipManager (item, no extra data, count, equip slot).
    virtual void ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId,
                                  int32_t aCount) noexcept = 0;
    /// Full unequip via EquipManager (item, no extra data, count, equip slot).
    virtual void ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId,
                                    int32_t aCount) noexcept = 0;
};
