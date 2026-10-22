#pragma once

struct TESObjectREFR;
struct TESQuest;
struct TESGlobal;
struct BGSStoryEvent;
struct BGSPackageDataList;

// What a condition runs on (CommonLibSSE-NG ConditionCheckParams); TESCondition::IsTrue (29888) builds one
// from its subject and target. How a list evaluates: docs/re/systems/conditions.md.
struct ConditionCheckParams
{
    TESObjectREFR* actionRef = nullptr;            // 00, the subject
    TESObjectREFR* targetRef = nullptr;            // 08
    TESQuest* quest = nullptr;                     // 10
    BGSStoryEvent* questStartEvent = nullptr;      // 18
    void* unk20 = nullptr;                         // 20
    bool unk28 = false;                            // 28, set by the list's evaluation (sub_14049FA70)
    BGSPackageDataList* packageDataList = nullptr; // 30
};
static_assert(sizeof(ConditionCheckParams) == 0x38);

// One condition (CTDA, CommonLibSSE-NG TESConditionItem / CONDITION_ITEM_DATA).
struct TESConditionItem
{
    enum class OpCode : uint8_t
    {
        kEqualTo,
        kNotEqualTo,
        kGreaterThan,
        kGreaterThanOrEqualTo,
        kLessThan,
        kLessThanOrEqualTo,
    };

    // Whom the function runs on (CONDITIONITEMOBJECT).
    enum class RunOn : uint8_t
    {
        kSubject,
        kTarget,
        kReference, // runOnRefHandle
        kCombatTarget,
        kLinkedRef,
        kQuestAlias,
        kPackData,
        kEventData,
        kCommandTarget,
    };

    struct Flags
    {
        uint8_t isOr : 1; // grouped with the items after it, up to the first without the flag
        uint8_t usesAliases : 1;
        uint8_t global : 1; // compares against a TESGlobal
        uint8_t usePackData : 1;
        uint8_t swapTarget : 1; // runs on the target, compared against the subject
        uint8_t opCode : 3;     // OpCode
    };

    // Its own result, as the engine evaluates it (TESConditionItem::IsTrue 29924).
    bool IsTrue(ConditionCheckParams& aParams) const noexcept;

    TESConditionItem* next; // 00
    union {
        float value;
        TESGlobal* global;   // flags.global
    } comparison;            // 08
    uint32_t runOnRefHandle; // 10, RunOn::kReference
    uint32_t dataId;         // 14
    uint16_t function;       // 18, the condition function (CommonLib FUNCTION_DATA::FunctionID)
    uint16_t pad1A;
    uint32_t pad1C;
    void* params[2]; // 20, the function's own parameters
    Flags flags;     // 30
    RunOn runOn;     // 31
    uint8_t pad32[6];
};
static_assert(sizeof(TESConditionItem::Flags) == 1);
static_assert(offsetof(TESConditionItem, comparison) == 0x08);
static_assert(offsetof(TESConditionItem, function) == 0x18);
static_assert(offsetof(TESConditionItem, flags) == 0x30);
static_assert(offsetof(TESConditionItem, runOn) == 0x31);
static_assert(sizeof(TESConditionItem) == 0x38);

// A form's condition list (CommonLibSSE-NG TESCondition).
struct TESCondition
{
    // The whole list for the subject on the target, OR groups and all (TESCondition::IsTrue 29888).
    bool IsTrue(TESObjectREFR* apSubject, TESObjectREFR* apTarget) const noexcept;

    TESConditionItem* pHead{};
};
