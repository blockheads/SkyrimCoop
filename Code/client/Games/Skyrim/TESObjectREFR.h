#pragma once

#include <Games/Primitives.h>
#include <Forms/TESForm.h>
#include <NetImmerse/BSFaceGenNiNode.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Animation/IAnimationGraphManagerHolder.h>
#include <Games/Misc/Lock.h>
#include <Games/Magic/MagicSystem.h>
#include <Magic/MagicCaster.h>
#include <Magic/MagicTarget.h>
#include <Structs/Inventory.h>
#include <ExtraData/ExtraDataList.h>

struct AnimationVariables;
struct TESWorldSpace;
struct TESBoundObject;
struct TESContainer;

enum class ITEM_REMOVE_REASON
{
    kRemove,
    kSteal,
    kSelling,
    kDropping,
    kStoreInContainer,
    kStoreInTeammate
};

struct TESObjectREFR : TESForm
{
    enum ChangeFlags : uint32_t
    {
        CHANGE_FORM_FLAGS = 1 << 0, // the disabled flag among them (TESForm::SetDisabled 14646)
        CHANGE_REFR_MOVE = 1 << 1,
        CHANGE_REFR_HAVOK_MOVE = 1 << 2,
        CHANGE_REFR_CELL_CHANGED = 1 << 3,
        CHANGE_REFR_SCALE = 1 << 4,
        CHANGE_REFR_INVENTORY = 1 << 5,
        CHANGE_REFR_EXTRA_OWNERSHIP = 1 << 6,
        CHANGE_REFR_BASEOBJECT = 1 << 7,
        CHANGE_OBJECT_EXTRA_LOCK = 1 << 12, // AddLockChange (19512)
        CHANGE_OBJECT_OPEN_STATE = 1 << 23, // BGSOpenCloseForm::SetOpenState (14287)
        CHANGE_REFR_PROMOTED = 1 << 25,
        CHANGE_REFR_EXTRA_ACTIVATING_CHILDREN = 1 << 26,
        CHANGE_REFR_LEVELED_INVENTORY = 1 << 27,
        CHANGE_REFR_ANIMATION = 1 << 28,
        CHANGE_REFR_EXTRA_ENCOUNTER_ZONE = 1 << 29,
        CHANGE_REFR_EXTRA_CREATED_ONLY = 1 << 30,
        CHANGE_REFR_EXTRA_GAME_ONLY = 1u << 31,
    };

    enum OpenState : uint8_t
    {
        kNone = 0,
        kOpen,
        kOpening,
        kClosed,
        kClosing,
    };

    static TESObjectREFR* GetByHandle(uint32_t aHandle) noexcept;
    // A reference not in memory, read from its plugin as a quest's alias fill reads one (the promote task's list,
    // 15328: its cell indoors, or its worldspace and grid cell outdoors), kept persistent for apOwner. Null if none.
    static TESObjectREFR* LoadFromPlugin(uint32_t aRefId, uint32_t aCellId, int16_t aGridX, int16_t aGridY, TESForm* apOwner) noexcept;
    // Off: a reference placed with a leveled base (ExtraLeveledItemBase, XLIB) keeps its placeholder when its cell
    // loads (the pick, 20232, skipped), its base coming from the host's pick.
    static void SetPicksLeveledBases(bool aPicks) noexcept;
    // Off: a container menu's take or store is proposed to the host (ContainerTransfers) instead of moved here.
    static void SetMovesContainerItems(bool aMoves) noexcept;
    static uint32_t* GetNullHandle() noexcept;

    static void GetItemFromExtraData(Inventory::Entry& arEntry, ExtraDataList* apExtraDataList) noexcept;
    // A new list with the item's extra data, none without any; aCounted: holding its count too, for an add.
    static ExtraDataList* GetExtraDataFromItem(const Inventory::Entry& arEntry, bool aCounted = false) noexcept;
    // The item's extra data (enchantment, charge, poison, tempering, soul, name) onto a list: a loose item's own.
    static void SetItemExtraData(ExtraDataList& aList, const Inventory::Entry& acEntry) noexcept;

    virtual void sub_3B();
    virtual void sub_3C();
    virtual void sub_3D();
    virtual void sub_3E();
    virtual void Update3DPosition(bool abWarp);
    virtual void UpdateSoundCallback(bool aEndSceneAction); // 40: starts and ends the queued voice line
    virtual void sub_41();
    virtual void sub_42();
    virtual void sub_43();
    virtual void sub_44();
    virtual void sub_45();
    virtual void sub_46();
    virtual void sub_47();
    virtual void sub_48();
    virtual void sub_49();
    virtual void sub_4A();
    virtual void sub_4B();
    virtual void sub_4C();
    virtual void sub_4D();
    virtual void sub_4E();
    virtual void StopCurrentDialogue(bool aForce);
    virtual void sub_50();
    virtual void sub_51();
    virtual void sub_52();
    virtual void sub_53();
    virtual void sub_54();
    virtual void sub_55();
    virtual BSPointerHandle<TESObjectREFR> RemoveItem(TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc = nullptr, const NiPoint3* apRotate = nullptr);
    virtual void sub_57();
    virtual void sub_58();
    virtual void sub_59();
    virtual void AddObjectToContainer(TESBoundObject* apObj, ExtraDataList* aspExtra, int32_t aicount, TESObjectREFR* apOldContainer);
    virtual void sub_5B();
    virtual MagicCaster* GetMagicCaster(MagicSystem::CastingSource aeSource);
    virtual MagicTarget* GetMagicTarget();
    virtual void sub_5E();
    virtual void sub_5F();
    virtual void sub_60();
    virtual BSFaceGenNiNode* GetFaceGenNiNode();
    virtual void sub_62();
    virtual void sub_63();
    virtual void sub_64();
    virtual void DetachHavok();
    virtual void sub_66();
    virtual void sub_67();
    virtual void sub_68();
    virtual void sub_69();
    virtual void sub_6A();
    virtual void sub_6B();
    virtual void sub_6C();
    virtual void sub_6D();
    virtual void sub_6E();
    virtual NiAVObject* Get3D1(bool aFirstPerson); // the player's first- or third-person 3D, others' 3D
    virtual NiNode* GetNiNode();
    virtual void sub_71();
    virtual void sub_72();
    virtual NiPoint3 GetBoundMin();
    virtual NiPoint3 GetBoundMax();
    virtual void sub_75();
    virtual void sub_76();
    virtual void sub_77();
    virtual void sub_78();
    virtual void sub_79();
    virtual void sub_7A();
    virtual void sub_7B();
    virtual void sub_7C();
    virtual void sub_7D();
    virtual void* sub_7E(bool aUnk);
    virtual void sub_7F();
    virtual void sub_80();
    virtual void sub_81();
    virtual void sub_82();
    virtual void sub_83();
    virtual void SetBaseForm(TESForm* apForm);
    virtual void sub_85();
    virtual void sub_86();
    virtual void sub_87();
    virtual void sub_88();
    virtual void DisableImpl();
    virtual void ResetInventory(bool abLeveledOnly);
    virtual void sub_8B();
    virtual void sub_8C();
    virtual void sub_8D();
    virtual void sub_8E();
    virtual void sub_8F();
    virtual void sub_90();
    virtual void sub_91();
    virtual void sub_92();
    virtual void sub_93();
    virtual void sub_94();
    virtual void sub_95();
    virtual void sub_96();
    virtual struct TESObjectCELL* GetParentCell() const;
    virtual void SetParentCell(struct TESObjectCELL* apParentCell);
    virtual bool VirtIsDead(bool abNotEssential); // TODO: Use this or papyrus IsDead?
    virtual void sub_9A();
    virtual void sub_9B();

    void SetRotation(float aX, float aY, float aZ) noexcept;

    BSPointerHandle<TESObjectREFR> GetHandle() const noexcept;
    uint32_t GetCellId() const noexcept;
    TESWorldSpace* GetWorldSpace() const noexcept;
    ExtraContainerChanges::Data* GetContainerChanges() const noexcept;
    // Its inventory changes, none made if it has none yet (TESObjectREFR::GetInventoryChanges 16039).
    ExtraContainerChanges::Data* FindContainerChanges() const noexcept;
    ExtraDataList* GetExtraDataList() noexcept;
    Lock* GetLock() const noexcept;
    TESContainer* GetContainer() const noexcept;
    int64_t GetItemCountInInventory(TESForm* apItem) const noexcept;
    // How many of the item it holds (InventoryChanges::GetItemCount 16108); none for no container or item.
    std::optional<int32_t> GetContainerCount(TESBoundObject* apItem) const noexcept;
    // Adds or removes the difference to hold that many, past our inventory hooks; false for no container or item.
    bool SetContainerCount(TESBoundObject* apItem, int32_t aCount) noexcept;
    // Every item it holds and how many: its base container's (leveled lists aside: rolled into the changes) and its
    // changes'. Rolls nothing.
    [[nodiscard]] std::vector<std::pair<TESBoundObject*, int32_t>> GetContainerItems() const noexcept;
    // Its base container's leveled entries (rolled when first needed).
    [[nodiscard]] uint32_t CountLeveledEntries() const noexcept;
    // Rolls its leveled entries as opening it does (InitInventoryIfRequired 16038); false for none to roll.
    bool InitInventoryIfRequired() noexcept;
    // Its base swapped (SetObjectReference, virtual 0x84) and recorded as the engine's pick records it (0x80); a loaded,
    // enabled reference's 3D loaded again for it.
    void SetBaseObject(TESBoundObject* apBase) noexcept;
    TESObjectCELL* GetParentCellEx() const noexcept;

    void SaveAnimationVariables(AnimationVariables& aWriter) const noexcept;
    void LoadAnimationVariables(const AnimationVariables& aReader) const noexcept;
    // The behaviour graphs' state as a savegame keeps it (Havok/AnimationGraphState.h), any reference's.
    bool SaveGraphs(std::string& aGraphs) const noexcept;
    bool LoadGraphs(const std::string& acGraphs) const noexcept;
    // An actor's, with its state flags.
    bool SaveAnimationGraphState(std::string& aState) const noexcept;
    bool LoadAnimationGraphState(const std::string& acState) const noexcept;
    uint32_t GetAnimationVariableInt(BSFixedString* apVariableName) noexcept;

    void RemoveAllItems() noexcept;
    std::vector<uint32_t> RemoveNonQuestItems(Inventory& aCurrentInventory) noexcept;
    void Delete() const noexcept;
    void Disable() const noexcept;
    void Enable() const noexcept;
    void MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept;
    // Places it on one axis ('X', 'Y', 'Z') with its 3D and collision, as the console's SetPos and Papyrus'
    // SetPosition do (22053); SetAngleOnAxis the same for its angle, in degrees as the console's SetAngle (22054).
    void SetPositionOnAxis(char aAxis, float aValue) noexcept;
    void SetAngleOnAxis(char aAxis, float aDegrees) noexcept;
    // A non-actor moved (aMoved): marks it moved again, as the engine records a loaded reference's move only
    // once (its 3D's moved bit), so the change funnel sees every one.
    void RecordMove(bool aMoved) noexcept;
    // As Papyrus' SetScale (19665): clamped to 0.01..10, the 3D and collision follow.
    void SetScale(float aScale) noexcept;
    // As Papyrus' SetActorOwner/SetFactionOwner (20198): an NPC, a faction or none, a load door's other side too.
    void SetOwner(TESForm* apOwner) noexcept;
    TESForm* GetOwner() const noexcept;
    bool SendAnimationEvent(BSFixedString* apEventName) noexcept;

    bool Activate(TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing) noexcept;

    bool PlayAnimationAndWait(BSFixedString* apAnimation, BSFixedString* apEventName) noexcept;
    bool PlayAnimation(BSFixedString* apEventName) noexcept;

    Lock* CreateLock() noexcept;
    void LockChange() noexcept;
    // Locks (at that level) or unlocks it and records the change, without raising LockChangeEvent.
    bool SetLockState(bool aLocked, uint8_t aLockLevel) noexcept;

    const float GetHeight() noexcept;
    void EnableImpl() noexcept;
    OpenState GetOpenState() noexcept;

    Inventory GetInventory() const noexcept;
    Inventory GetInventory(std::function<bool(TESForm&)> aFilter) const noexcept;
    Inventory GetArmor() const noexcept;
    Inventory GetWornArmor() const noexcept;

    bool IsItemInInventory(uint32_t aFormID) const noexcept;

    void SetInventory(const Inventory& acContainer) noexcept;
    void SetInventoryRetainingQuestItems(Inventory& aCurrentInventory, const Inventory& acSourceInventory) noexcept;
    void AddOrRemoveItem(const Inventory::Entry& arEntry, bool aIsSettingInventory = false) noexcept;
    void UpdateItemList(TESForm* pUnkForm) noexcept;

    BSHandleRefObject handleRefObject;
    uintptr_t unk1C;
    IAnimationGraphManagerHolder animationGraphHolder;
    TESForm* baseForm; // 40 SE
    NiPoint3 rotation;
    NiPoint3 position;
    TESObjectCELL* parentCell;
    void* loadedState;
    ExtraDataList extraData;
    BSRecursiveLock refLock;
    uint16_t scale;
    uint16_t referenceFlags;
};

static_assert(sizeof(TESObjectREFR) == 0xA0);
static_assert(offsetof(TESObjectREFR, loadedState) == 0x68);
