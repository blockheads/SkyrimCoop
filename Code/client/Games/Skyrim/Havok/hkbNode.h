#pragma once

#include <Havok/hkbVariableInfo.h>

// Behaviour graph nodes as the variable writers walk reads them: Havok 2010.2 layouts (hkxcmd compat/,
// CommonLibSSE-NG) checked in IDA. docs/design/graph-replication.md.

struct hkClass
{
    const char* name; // 0
};

template <class T> struct hkArrayView
{
    T* data;          // 0
    int32_t size;     // 8
    int32_t capacity; // C
};
static_assert(sizeof(hkArrayView<int>) == 0x10);

// hkPointerMap's storage (hkPointerMap_GetWithDefault 57195): open addressing over hashMod + 1 slots, empty key ~0.
struct hkPointerMapView
{
    struct Pair
    {
        uintptr_t key;
        uintptr_t value;
    };
    Pair* elements;  // 0
    int32_t count;   // 8
    int32_t hashMod; // C

    // As 57195 looks up.
    [[nodiscard]] uintptr_t Get(uintptr_t aKey) const noexcept
    {
        if (hashMod <= 0 || !elements)
            return 0;
        for (uint32_t i = hashMod & (uint32_t(aKey >> 4) * 0x9E3779B1u);; i = hashMod & (i + 1))
        {
            if (elements[i].key == ~uintptr_t(0))
                return 0;
            if (elements[i].key == aKey)
                return elements[i].value;
        }
    }
};
static_assert(sizeof(hkPointerMapView) == 0x10);

struct hkbVariableBindingSet
{
    struct Binding
    {
        enum : uint8_t
        {
            kTypeVariable = 0,
            kTypeCharacterProperty = 1,
        };
        enum : uint8_t
        {
            kFlagOutput = 1, // set at load from the member's hkb.RoleAttribute FLAG_OUTPUT (59474)
        };

        const char* memberPath; // 0
        void* memberClass;      // 8
        int32_t offsetInObjectPlusOne;
        int32_t offsetInArrayPlusOne;
        int32_t rootVariableIndex; // 18
        int32_t variableIndex;     // 1C
        int8_t bitIndex;           // 20
        uint8_t bindingType;       // 21
        uint8_t memberType;        // 22
        int8_t variableType;       // 23
        uint8_t flags;             // 24
        uint8_t pad25[3];
    };

    // Whether the binding writes its variable, resolved from the class reflection as the engine's own
    // cacheBindables (58524) does it on a node's first activation; works on a copy, any node.
    static bool IsOutput(const Binding& acBinding, const struct hkbNode* apNode) noexcept;

    void* vtable;
    uint64_t refCount;
    hkArrayView<Binding> bindings; // 10
    int32_t indexOfBindingToEnable;
    bool hasOutputBinding; // 24
    uint8_t pad25[3];
};
static_assert(sizeof(hkbVariableBindingSet::Binding) == 0x28);
static_assert(offsetof(hkbVariableBindingSet::Binding, variableIndex) == 0x1C);
static_assert(offsetof(hkbVariableBindingSet::Binding, flags) == 0x24);
static_assert(offsetof(hkbVariableBindingSet, bindings) == 0x10);
static_assert(offsetof(hkbVariableBindingSet, hasOutputBinding) == 0x24);
static_assert(sizeof(hkbVariableBindingSet) == 0x28);

// hkbNode : hkbBindable : hkReferencedObject.
struct hkbNode
{
    virtual ~hkbNode();
    virtual const hkClass* GetClassType() const;
    virtual void sub_02();
    virtual void sub_03();
    virtual void sub_04();
    virtual void sub_05();
    virtual void sub_06();
    virtual void sub_07();
    virtual void sub_08();
    virtual void sub_09();
    virtual void sub_0A();
    virtual void sub_0B();
    virtual void sub_0C(); // cloneNode
    // Its memory between updates (a range modifier's last ranges, a damping modifier's last values), which Skyrim's
    // graph save leaves out (hkbEventsFromRangeModifier 59728-59730; coop_hkbBehaviorGraph_getInternalState 58372).
    virtual struct hkReferencedObject* CreateInternalState();                 // 0D
    virtual void GetInternalState(struct hkReferencedObject& aState) const;   // 0E
    virtual void GetInternalStateUser(const void* apGraph, struct hkReferencedObject& aState); // 0F
    virtual void SetInternalState(const struct hkReferencedObject& acState);  // 10

    uint64_t refCount;
    hkbVariableBindingSet* bindings; // 10
    uint8_t pad18[0x38 - 0x18];
    const char* name; // 38
    uint16_t id;      // 40
    uint8_t cloneState;
    uint8_t pad43[0x48 - 0x43];
};
static_assert(offsetof(hkbNode, bindings) == 0x10);
static_assert(offsetof(hkbNode, name) == 0x38);
static_assert(sizeof(hkbNode) == 0x48);

struct hkbBehaviorGraphStringData
{
    void* vtable;
    uint64_t refCount;
    hkArrayView<const char*> eventNames;     // 10
    hkArrayView<const char*> attributeNames; // 20
    hkArrayView<const char*> variableNames;  // 30
};
static_assert(offsetof(hkbBehaviorGraphStringData, variableNames) == 0x30);

struct hkbBehaviorGraphData
{
    void* vtable;
    uint64_t refCount;
    uint8_t pad10[0x20 - 0x10];
    hkArrayView<hkbVariableInfo> variableInfos; // 20
    uint8_t pad30[0x78 - 0x30];
    hkbBehaviorGraphStringData* stringData; // 78
};
static_assert(offsetof(hkbBehaviorGraphData, variableInfos) == 0x20);
static_assert(offsetof(hkbBehaviorGraphData, stringData) == 0x78);

struct hkaAnimation
{
    uint8_t pad0[0x14];
    float duration; // 14
};
static_assert(offsetof(hkaAnimation, duration) == 0x14);

struct hkaAnimationBinding
{
    uint8_t pad0[0x18];
    hkaAnimation* animation; // 18, null until its file is loaded
};
static_assert(offsetof(hkaAnimationBinding, animation) == 0x18);

struct hkaDefaultAnimationControl
{
    uint8_t pad0[0x38];
    hkaAnimationBinding* binding; // 38
};
static_assert(offsetof(hkaDefaultAnimationControl, binding) == 0x38);

// A clip playing in a graph (CommonLib hkbClipGenerator). With no animation its duration reads 0.01333 (59263).
struct hkbClipGenerator : hkbNode
{
    uint8_t pad48[0x88 - 0x48];
    hkaDefaultAnimationControl* animationControl; // 88
    uint8_t pad90[0xF0 - 0x90];
    float localTime; // F0
    float time;      // F4
    uint8_t padF8[0x104 - 0xF8];
    bool atEnd; // 104
};
static_assert(offsetof(hkbClipGenerator, animationControl) == 0x88);
static_assert(offsetof(hkbClipGenerator, localTime) == 0xF0);
static_assert(offsetof(hkbClipGenerator, atEnd) == 0x104);

// One active node of a behaviour graph (CommonLib hkbNodeInfo).
struct hkbNodeInfo
{
    uint8_t pad0[0x58];
    hkbNode* nodeClone; // 58
    uint8_t pad60[0x90 - 0x60];
};
static_assert(offsetof(hkbNodeInfo, nodeClone) == 0x58);
static_assert(sizeof(hkbNodeInfo) == 0x90);

// What a graph's random picks draw from: seed = seed * 1664525 + 1013904223 (59406, 60025, 63213).
struct hkPseudoRandomGenerator
{
    uint32_t seed;    // 0
    uint32_t current; // 4
};
static_assert(offsetof(hkPseudoRandomGenerator, current) == 4);

// Sends its event after a random number of another (63213): a shopkeeper's loop ending in crossed arms.
struct BSEventEveryNEventsModifier : hkbNode
{
    uint8_t pad48[0x70 - 0x48];
    int8_t numberOfEventsBeforeSend;        // 70
    int8_t minimumNumberOfEventsBeforeSend; // 71
    bool randomizeNumberOfEvents;           // 72
    uint8_t pad73;
    int32_t numberOfEventsSeen;            // 74
    int8_t calculatedNumberOfEventsBefore; // 78
};
static_assert(offsetof(BSEventEveryNEventsModifier, numberOfEventsSeen) == 0x74);
static_assert(offsetof(BSEventEveryNEventsModifier, calculatedNumberOfEventsBefore) == 0x78);

// Behaviour graph as a node: its root generator and its own variable names (sub-behaviours index
// variables locally).
struct hkbBehaviorGraphNode : hkbNode
{
    uint8_t pad48[0x78 - 0x48];
    hkPseudoRandomGenerator* random; // 78
    hkbNode* rootGenerator;          // 80
    hkbBehaviorGraphData* data;      // 88
    uint8_t pad90[0x98 - 0x90];
    hkArrayView<hkbNodeInfo>* activeNodes; // 98
    uint8_t padA0[0xE0 - 0xA0];
    hkPointerMapView* nodeTemplateToCloneMap; // E0, a node's clone for this character (GetNodeClone 58359)
    hkPointerMapView* nodeCloneToTemplateMap; // E8
};
static_assert(offsetof(hkbBehaviorGraphNode, random) == 0x78);
static_assert(offsetof(hkbBehaviorGraphNode, rootGenerator) == 0x80);
static_assert(offsetof(hkbBehaviorGraphNode, data) == 0x88);
static_assert(offsetof(hkbBehaviorGraphNode, activeNodes) == 0x98);
static_assert(offsetof(hkbBehaviorGraphNode, nodeTemplateToCloneMap) == 0xE0);
static_assert(offsetof(hkbBehaviorGraphNode, nodeCloneToTemplateMap) == 0xE8);

struct hkbBehaviorReferenceGenerator : hkbNode
{
    const char* behaviorName;       // 48
    hkbBehaviorGraphNode* behavior; // 50, linked at load (63948)
};
static_assert(offsetof(hkbBehaviorReferenceGenerator, behavior) == 0x50);

struct hkbStateMachineNode : hkbNode
{
    struct StateInfo
    {
        uint8_t pad0[0x60];
        const char* name; // 60
        int32_t stateId;  // 68
    };
    uint8_t pad48[0x68 - 0x48];
    int32_t startStateId;      // 68
    uint8_t pad6C[0x7C - 0x6C];
    int32_t syncVariableIndex; // 7C, written with the current state's id
    int32_t currentStateId;    // 80
    uint8_t pad84[0x86 - 0x84];
    uint8_t startStateMode; // 86: 0 startStateId, 1 sync, 2 random, 3 chooser
    uint8_t pad87;
    bool isActive; // 88, Activate (59334) enters a start state only when false
    uint8_t pad89[0x90 - 0x89];
    hkArrayView<StateInfo*> states; // 90
    uint8_t padA0[0xFC - 0xA0];
    int32_t nextStartStateIndexOverride; // FC, an index; the next Activate starts there (59405), -1 none
};
static_assert(offsetof(hkbStateMachineNode::StateInfo, stateId) == 0x68);
static_assert(offsetof(hkbStateMachineNode, startStateId) == 0x68);
static_assert(offsetof(hkbStateMachineNode, syncVariableIndex) == 0x7C);
static_assert(offsetof(hkbStateMachineNode, currentStateId) == 0x80);
static_assert(offsetof(hkbStateMachineNode, startStateMode) == 0x86);
static_assert(offsetof(hkbStateMachineNode, isActive) == 0x88);
static_assert(offsetof(hkbStateMachineNode, states) == 0x90);
static_assert(offsetof(hkbStateMachineNode, nextStartStateIndexOverride) == 0xFC);

struct hkbExpressionData
{
    const char* expression;          // 0, "Variable = ..." assigns
    int32_t assignmentVariableIndex; // 8
    int32_t assignmentEventIndex;    // C
    int8_t eventMode;
    bool raisedEvent;
    bool wasTrueInPreviousFrame;
    uint8_t pad13[0x18 - 0x13];
};
static_assert(sizeof(hkbExpressionData) == 0x18);

struct hkbExpressionDataArray
{
    void* vtable;
    uint64_t refCount;
    hkArrayView<hkbExpressionData> expressions; // 10
};

struct hkbEvaluateExpressionModifier : hkbNode
{
    uint8_t pad48[0x50 - 0x48];
    hkbExpressionDataArray* expressions; // 50
};
static_assert(offsetof(hkbEvaluateExpressionModifier, expressions) == 0x50);

// The engine's breadth-first walk of every node under a root, each once (63865/63867; Open Animation
// Replacer uses it). Flags: hkbNode::GetChildrenFlagBits.
struct hkbNodeIterator
{
    enum : uint32_t
    {
        kActiveOnly = 1,
        kGeneratorsOnly = 2,
        kIgnoreReferencedBehaviour = 4,
    };

    hkbNodeIterator(uint32_t aFlags, hkbNode* apRoot) noexcept;
    ~hkbNodeIterator() noexcept;
    hkbNodeIterator(const hkbNodeIterator&) = delete;
    hkbNodeIterator& operator=(const hkbNodeIterator&) = delete;

    hkbNode* Next() noexcept;

    uint8_t storage[0x60];
};
static_assert(sizeof(hkbNodeIterator) == 0x60);
