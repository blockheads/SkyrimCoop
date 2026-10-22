#pragma once

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
    uint8_t pad10[0x78 - 0x10];
    hkbBehaviorGraphStringData* stringData; // 78
};
static_assert(offsetof(hkbBehaviorGraphData, stringData) == 0x78);

// Behaviour graph as a node: its root generator and its own variable names (sub-behaviours index
// variables locally).
struct hkbBehaviorGraphNode : hkbNode
{
    uint8_t pad48[0x80 - 0x48];
    hkbNode* rootGenerator;     // 80
    hkbBehaviorGraphData* data; // 88
};
static_assert(offsetof(hkbBehaviorGraphNode, rootGenerator) == 0x80);
static_assert(offsetof(hkbBehaviorGraphNode, data) == 0x88);

struct hkbBehaviorReferenceGenerator : hkbNode
{
    const char* behaviorName;       // 48
    hkbBehaviorGraphNode* behavior; // 50, linked at load (63948)
};
static_assert(offsetof(hkbBehaviorReferenceGenerator, behavior) == 0x50);

struct hkbStateMachineNode : hkbNode
{
    uint8_t pad48[0x7C - 0x48];
    int32_t syncVariableIndex; // 7C, written with the current state's id
};
static_assert(offsetof(hkbStateMachineNode, syncVariableIndex) == 0x7C);

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
