#include <Havok/GraphInternalState.h>

#include <Actor.h>
#include <Games/Animation/GraphState.h>
#include <Games/Animation/NodeMemory.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbCharacter.h>
#include <Havok/hkbNode.h>
#include <Havok/hkEventContext.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Wire/Serialization.h>
#include <bit>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
// An hkStringPtr's text: its lowest bit flags a string it owns.
const char* Text(const char* apStringPtr) noexcept
{
    return reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(apStringPtr) & ~uintptr_t(1));
}

std::string_view ClassName(const hkbNode& acNode)
{
    const hkClass* pClass = acNode.GetClassType();
    return pClass && pClass->name ? std::string_view(pClass->name) : std::string_view{};
}

uint32_t CheckOf(const hkbNode& acNode)
{
    return GraphState::Check(ClassName(acNode), acNode.name ? Text(acNode.name) : "");
}

void AddReference(void* apObject)
{
    using TAdd = void(void*);
    POINTER_SKYRIMSE(TAdd, s_add, 57010);
    s_add.Get()(apObject);
}

void RemoveReference(void* apObject)
{
    using TRemove = void(void*);
    POINTER_SKYRIMSE(TRemove, s_remove, 57011);
    s_remove.Get()(apObject);
}

// hkContainerAllocators::singleton: the allocator Havok's arrays use (bufFree: vtable 4).
void* Containers()
{
    POINTER_SKYRIMSE(uint8_t, s_containers, 383828);
    return s_containers.Get();
}

void FreeArray(void* apData, int32_t aCapacityAndFlags, uint32_t aElementSize)
{
    if (!apData || aCapacityAndFlags < 0) // DONT_DEALLOCATE
        return;
    using TFree = void(void*, void*, int32_t);
    void* pAllocator = Containers();
    (*reinterpret_cast<TFree***>(pAllocator))[4](pAllocator, apData, int32_t(aElementSize * uint32_t(aCapacityAndFlags & 0x3FFFFFFF)));
}

// A running transition's: its effect remade by setInternalStateUser (59343), its effect's own state carried (59342).
NodeMemory::Pointer PointerKind(std::string_view aClass, std::string_view aMember)
{
    if (aClass == "hkbStateMachineActiveTransitionInfo")
    {
        if (aMember == "transitionEffect")
            return NodeMemory::Pointer::kSkip;
        if (aMember == "transitionEffectInternalStateInfo")
            return NodeMemory::Pointer::kNested;
    }
    return NodeMemory::Pointer::kUnknown;
}

// A class's reflection for NodeMemory, built once (its own members; hkReferencedObject's are not memory).
const NodeMemory::Class* Reflected(const hkClass* apClass, int aDepth = 0)
{
    static std::recursive_mutex s_lock;
    static std::unordered_map<const hkClass*, std::unique_ptr<NodeMemory::Class>> s_classes;
    if (!apClass || aDepth > 4)
        return nullptr;
    std::scoped_lock _{s_lock};
    if (const auto it = s_classes.find(apClass); it != s_classes.end())
        return it->second.get();
    auto pClass = std::make_unique<NodeMemory::Class>();
    pClass->Size = uint32_t(std::max(apClass->objectSize, 0));
    for (int32_t i = 0; i < apClass->numDeclaredMembers; ++i)
    {
        const hkClassMember& cMember = apClass->declaredMembers[i];
        const bool cStruct = cMember.type == NodeMemory::kStruct || (cMember.type == NodeMemory::kArray && cMember.subtype == NodeMemory::kStruct);
        pClass->Members.push_back({cMember.offset, cMember.type, cMember.subtype, uint16_t(std::max<int16_t>(cMember.cArraySize, 0)),
                                   cStruct ? Reflected(cMember.klass, aDepth + 1) : nullptr,
                                   PointerKind(apClass->name ? apClass->name : "", cMember.name ? cMember.name : "")});
    }
    return s_classes.emplace(apClass, std::move(pClass)).first->second.get();
}

// Every class the game links, by name (hkBuiltinTypeRegistry::StaticLinkedClasses 239939).
const hkClass* ClassNamed(const std::string& acName)
{
    static std::mutex s_lock;
    static std::unordered_map<std::string, const hkClass*> s_byName;
    std::scoped_lock _{s_lock};
    if (s_byName.empty())
    {
        POINTER_SKYRIMSE(const hkClass*, s_linked, 239939);
        for (const hkClass* const* ppClass = s_linked.Get(); ppClass && *ppClass; ++ppClass)
            if ((*ppClass)->name)
                s_byName.emplace((*ppClass)->name, *ppClass);
    }
    const auto it = s_byName.find(acName);
    return it == s_byName.end() ? nullptr : it->second;
}

// A node class's internal state class (<class>InternalState): a state object's own getClassType returns 0.
const NodeMemory::Class* StateOf(std::string_view aNodeClass)
{
    const auto* pReflected = Reflected(ClassNamed(std::string(aNodeClass) + "InternalState"));
    return pReflected && !NodeMemory::HasPointers(*pReflected) ? pReflected : nullptr;
}

// A node's plain members, of its class and its parents up to the node bases: a BS node's runtime ones, which have no
// internal state (BSTimerModifier::secondsElapsed), and the ones another node writes (a blender's blendParameter,
// frozen by BSCyclicBlendTransitionGenerator 63132). The rest equal the template's on every game.
const NodeMemory::Class* RuntimeOf(const hkClass* apNodeClass)
{
    static std::mutex s_lock;
    static std::unordered_map<const hkClass*, std::unique_ptr<NodeMemory::Class>> s_classes;
    if (!apNodeClass)
        return nullptr;
    std::scoped_lock _{s_lock};
    if (const auto it = s_classes.find(apNodeClass); it != s_classes.end())
        return it->second.get();
    auto pClass = std::make_unique<NodeMemory::Class>();
    pClass->Size = uint32_t(std::max(apNodeClass->objectSize, 0));
    for (const hkClass* pLevel = apNodeClass; pLevel && pLevel->name; pLevel = pLevel->parent)
    {
        const std::string_view cName = pLevel->name;
        if (cName == "hkbNode" || cName == "hkbGenerator" || cName == "hkbModifier" || cName == "hkbTransitionEffect" ||
            cName == "hkbBindable" || cName == "hkReferencedObject")
            break;
        for (int32_t i = 0; i < pLevel->numDeclaredMembers; ++i)
        {
            const hkClassMember& cMember = pLevel->declaredMembers[i];
            const uint8_t cValue = cMember.type == NodeMemory::kEnum || cMember.type == NodeMemory::kFlags ? cMember.subtype : cMember.type;
            const auto* pStruct = cMember.type == NodeMemory::kStruct ? Reflected(cMember.klass) : nullptr;
            if (NodeMemory::PlainSize(cValue) || (pStruct && !NodeMemory::HasPointers(*pStruct)))
                pClass->Members.push_back({cMember.offset, cMember.type, cMember.subtype, uint16_t(std::max<int16_t>(cMember.cArraySize, 0)), pStruct});
        }
    }
    if (pClass->Members.empty())
        pClass.reset();
    return s_classes.emplace(apNodeClass, std::move(pClass)).first->second.get();
}

std::string Bytes(const Wire::Buffer& acBuffer, const Wire::Buffer::Writer& acWriter)
{
    return std::string(reinterpret_cast<const char*>(acBuffer.GetData()), acWriter.Size());
}

Wire::Buffer::Reader ReaderOf(const std::string& acBytes)
{
    return Wire::Buffer::Reader{std::span(reinterpret_cast<const uint8_t*>(acBytes.data()), acBytes.size())};
}

// Where setInternalStateUser (59343) finds a running transition's effect: a state's transitions or a machine's
// wildcards (another machine's, by id).
const hkbNode* EffectTemplate(const hkbStateMachineTransitions& acMachine, const hkbStateMachineTransitionInfoReference& acReference,
                              const std::unordered_map<uint16_t, hkbNode*>& acTemplates)
{
    const hkbStateMachineTransitionInfoArray* pTransitions = nullptr;
    if (acReference.fromStateIndex == -1)
    {
        const hkbStateMachineTransitions* pMachine = &acMachine;
        if (acReference.stateMachineId != -1)
        {
            const auto it = acTemplates.find(uint16_t(acReference.stateMachineId));
            if (it == acTemplates.end() || ClassName(*it->second) != "hkbStateMachine")
                return nullptr;
            pMachine = static_cast<const hkbStateMachineTransitions*>(it->second);
        }
        pTransitions = pMachine->wildcardTransitions;
    }
    else if (acReference.fromStateIndex >= 0 && acReference.fromStateIndex < acMachine.states.size && acMachine.states.data[acReference.fromStateIndex])
        pTransitions = acMachine.states.data[acReference.fromStateIndex]->transitions;
    if (!pTransitions || acReference.transitionIndex < 0 || acReference.transitionIndex >= pTransitions->transitions.size)
        return nullptr;
    return pTransitions->transitions.data[acReference.transitionIndex].transition;
}

// Skyrim's updateActiveNodes (60079) walks with 59319, which drops its restore map; while a restore runs, that walk
// goes to Havok's own 59318 instead, whose 59326 gives each node its state as it activates.
thread_local bool s_restoring = false;
using TWalk = void*(hkbNode*, void*, uint32_t, void*, void*, void*, void*, void*, void*, uint8_t, void*);
TWalk* RealCollectLeafFirst = nullptr;
void* HookCollectLeafFirst(hkbNode* apRoot, void* apGraph, uint32_t aFlags, void* apList, void* apMap, void* apChildren, void* apOldList,
                           void* apOldMap, void* apContext, uint8_t aSync, void* apRestore)
{
    if (s_restoring && apRestore)
    {
        POINTER_SKYRIMSE(TWalk, s_activateNodes, 59318);
        return s_activateNodes.Get()(apRoot, apGraph, aFlags, apList, apMap, apChildren, apOldList, apOldMap, apContext, aSync, apRestore);
    }
    return RealCollectLeafFirst(apRoot, apGraph, aFlags, apList, apMap, apChildren, apOldList, apOldMap, apContext, aSync, apRestore);
}
Hooks::Initializer s_restoreWalk([]() { HOOK_ID(59319, &RealCollectLeafFirst, HookCollectLeafFirst); });

// Its active clips' local times (hkbClipGenerator +0xF0), for the logs.
std::string ClipTimes(const hkbBehaviorGraphNode& acGraph)
{
    std::string times;
    for (int32_t i = 0; acGraph.activeNodes && i < acGraph.activeNodes->size; ++i)
        if (const hkbNode* pNode = acGraph.activeNodes->data[i].nodeClone; pNode && ClassName(*pNode) == "hkbClipGenerator")
            times += std::format("{}={:08X} ", pNode->name ? Text(pNode->name) : "", std::bit_cast<uint32_t>(static_cast<const hkbClipGenerator*>(pNode)->localTime));
    return times;
}

struct StateHooks;
GraphState::Node NodeOf(const hkbNodeInternalStateInfo& acInfo, const hkbNode& acNode, StateHooks& aHooks);

// A state's arrays and its machine's running transitions' effect states, on each side.
struct StateHooks final : NodeMemory::Hooks
{
    // Copy: the machine whose state is read and its game's templates by id.
    const hkbStateMachineTransitions* Machine = nullptr;
    const std::unordered_map<uint16_t, hkbNode*>* Templates = nullptr;
    struct Pending
    {
        hkbStateMachineActiveTransitionInfo* Transition;
        GraphState::Node Effect;
    };
    std::vector<Pending> Waiting;
    std::vector<std::unique_ptr<hkbNodeInternalStateInfo>> Infos; // ours: Havok never frees them
    std::vector<hkReferencedObject*> States;                       // made here, released after the restore
    std::vector<hkbNodeInternalStateInfo**> Slots;                 // ours in machines' states: nulled before those go
    uint32_t Effects = 0, LostEffects = 0;

    bool Reserve(NodeMemory::ArrayView& aArray, uint32_t aCount, uint32_t aElementSize) override
    {
        using TReserve = int32_t(void*, NodeMemory::ArrayView*, int32_t, int32_t);
        POINTER_SKYRIMSE(TReserve, s_reserve, 57048);
        s_reserve.Get()(Containers(), &aArray, int32_t(aCount), int32_t(aElementSize));
        return aArray.Data && (aArray.CapacityAndFlags & 0x3FFFFFFF) >= int32_t(aCount);
    }

    void WriteNested(Wire::Buffer::Writer& aWriter, const uint8_t* acpObject, const NodeMemory::Member&) override
    {
        const auto& cTransition = *reinterpret_cast<const hkbStateMachineActiveTransitionInfo*>(acpObject);
        const bool cPresent = cTransition.transitionEffectInternalStateInfo && cTransition.transitionEffect;
        aWriter.WriteBits(cPresent ? 1 : 0, 1);
        if (cPresent)
        {
            const std::string cEffect = GraphState::Encode({NodeOf(*cTransition.transitionEffectInternalStateInfo, *cTransition.transitionEffect, *this)});
            Wire::Serialization::WriteVarInt(aWriter, cEffect.size());
            aWriter.WriteBytes({reinterpret_cast<const uint8_t*>(cEffect.data()), cEffect.size()});
        }
    }

    bool ReadNested(Wire::Buffer::Reader& aReader, uint8_t* apObject, const NodeMemory::Member&) override
    {
        auto& transition = *reinterpret_cast<hkbStateMachineActiveTransitionInfo*>(apObject);
        transition.transitionEffectInternalStateInfo = nullptr;
        uint64_t present = 0, size = 0;
        if (!aReader.ReadBits(present, 1))
            return false;
        if (!present)
            return true;
        if (!Wire::Serialization::TryReadVarInt(aReader, size) || size > (1u << 20))
            return false;
        std::string bytes(size, '\0');
        std::vector<GraphState::Node> effect;
        if (!aReader.ReadBytes({reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()}) || !GraphState::Decode(bytes, effect) || effect.size() != 1)
            return false;
        Waiting.push_back({&transition, std::move(effect[0])}); // its effect's references come after it in the state
        return true;
    }

    // After a machine's state read: each running transition's effect state, made by the effect its machine will
    // clone, behind an info of ours.
    void Resolve()
    {
        for (auto& pending : Waiting)
        {
            ++Effects;
            const hkbNode* pEffect = Machine && Templates ? EffectTemplate(*Machine, pending.Transition->transitionInfoReferenceForTE, *Templates) : nullptr;
            const auto* pClass = pEffect && CheckOf(*pEffect) == pending.Effect.Check ? StateOf(ClassName(*pEffect)) : nullptr;
            hkReferencedObject* pState = pClass && pending.Effect.State ? const_cast<hkbNode*>(pEffect)->CreateInternalState() : nullptr;
            if (!pState)
            {
                ++LostEffects;
                continue;
            }
            auto reader = ReaderOf(*pending.Effect.State);
            if (!NodeMemory::Read(reader, reinterpret_cast<uint8_t*>(pState), *pClass, this))
            {
                RemoveReference(pState);
                ++LostEffects;
                continue;
            }
            auto pInfo = std::make_unique<hkbNodeInternalStateInfo>();
            std::memcpy(pInfo->syncInfo, &pending.Effect.Sync, sizeof(pInfo->syncInfo));
            pInfo->hasActivateBeenCalled = pending.Effect.Activated;
            pInfo->internalState = pState;
            pending.Transition->transitionEffectInternalStateInfo = pInfo.get();
            Slots.push_back(&pending.Transition->transitionEffectInternalStateInfo);
            States.push_back(pState);
            Infos.push_back(std::move(pInfo));
        }
        Waiting.clear();
    }
};

// One node's entry from getInternalState's info: its id, its node's check, sync, and its state's bytes.
GraphState::Node NodeOf(const hkbNodeInternalStateInfo& acInfo, const hkbNode& acNode, StateHooks& aHooks)
{
    GraphState::Node node;
    node.Id = uint16_t(acInfo.nodeId);
    node.Check = CheckOf(acNode);
    std::memcpy(&node.Sync, acInfo.syncInfo, sizeof(node.Sync));
    node.Activated = acInfo.hasActivateBeenCalled;
    if (acInfo.internalState)
        if (const auto* pClass = StateOf(ClassName(acNode)))
        {
            Wire::Buffer buffer;
            Wire::Buffer::Writer writer(&buffer);
            NodeMemory::Write(writer, reinterpret_cast<const uint8_t*>(acInfo.internalState), *pClass, &aHooks);
            node.State = Bytes(buffer, writer);
        }
    return node;
}
} // namespace

namespace GraphInternalState
{
std::string Take(const BShkbAnimationGraph& acGraph)
{
    auto* pGraph = reinterpret_cast<hkbBehaviorGraphNode*>(acGraph.behaviorGraph);
    if (!pGraph || !pGraph->activeNodes)
        return {};
    std::unordered_map<uint16_t, const hkbNode*> active;
    for (int32_t i = 0; i < pGraph->activeNodes->size; ++i)
        if (const hkbNode* pClone = pGraph->activeNodes->data[i].nodeClone)
        {
            // A cross-blend's effect and second blender are no point's to carry: none until it ends.
            if (ClassName(*pClone) == "BSCyclicBlendTransitionGenerator" &&
                !GraphState::CarriesCyclicMode(static_cast<const BSCyclicBlendTransitionGenerator*>(pClone)->currentMode))
            {
                spdlog::debug("Graph stream: {:X}'s sync point waits for {}'s cross-blend", acGraph.holder ? acGraph.holder->formID : 0,
                              pClone->name ? Text(pClone->name) : "");
                return {};
            }
            active.emplace(pClone->id, pClone);
        }

    using TGetState = void(void*, hkbBehaviorGraphInternalState*);
    POINTER_SKYRIMSE(TGetState, s_getInternalState, 58372);
    hkbBehaviorGraphInternalState state{};
    state.nodeInternalStateInfos = {nullptr, 0, int32_t(0x80000000)};
    s_getInternalState.Get()(pGraph, &state);

    StateHooks hooks;
    std::vector<GraphState::Node> nodes;
    uint32_t states = 0, runtimes = 0;
    for (int32_t i = 0; i < state.nodeInternalStateInfos.size; ++i)
    {
        hkbNodeInternalStateInfo* pInfo = state.nodeInternalStateInfos.data[i];
        if (!pInfo)
            continue;
        if (const auto it = active.find(uint16_t(pInfo->nodeId)); it != active.end())
        {
            auto node = NodeOf(*pInfo, *it->second, hooks);
            states += node.State.has_value();
            if (const auto* pRuntime = RuntimeOf(it->second->GetClassType()))
            {
                Wire::Buffer buffer;
                Wire::Buffer::Writer writer(&buffer);
                NodeMemory::Write(writer, reinterpret_cast<const uint8_t*>(it->second), *pRuntime);
                node.Runtime = Bytes(buffer, writer);
                ++runtimes;
            }
            nodes.push_back(std::move(node));
        }
        RemoveReference(pInfo); // its state with it (58398)
    }
    FreeArray(state.nodeInternalStateInfos.data, state.nodeInternalStateInfos.capacity, sizeof(void*));
    if (state.variableValueSet)
        RemoveReference(state.variableValueSet);
    const auto* pQueue = acGraph.character.eventQueue;
    spdlog::debug("Graph stream: {:X}'s sync point of {} active nodes ({} states, {} runtime members), {} events waiting; clips {}",
                  acGraph.holder ? acGraph.holder->formID : 0, nodes.size(), states, runtimes, pQueue ? pQueue->count : -1, ClipTimes(*pGraph));
    return GraphState::Encode(nodes);
}

bool Give(BShkbAnimationGraph& aGraph, hkEventContext& aContext, const std::string& acState, std::span<const uint32_t> aWords)
{
    auto* pBehaviour = aGraph.behaviorGraph;
    auto* pGraph = reinterpret_cast<hkbBehaviorGraphNode*>(pBehaviour);
    std::vector<GraphState::Node> nodes;
    if (!pGraph || !pGraph->rootGenerator || !pBehaviour->animationVariables || !GraphState::Decode(acState, nodes))
    {
        spdlog::debug("Graph stream: {:X} takes no sync point: {}", aGraph.holder ? aGraph.holder->formID : 0,
                      !pGraph || !pGraph->rootGenerator ? "no root generator" : !pBehaviour->animationVariables ? "no variables" : "its state does not decode");
        return false;
    }

    // Its templates by id: numbered alike from the same behaviour files (setNodeIds 58369).
    std::unordered_map<uint16_t, hkbNode*> templates;
    templates.emplace(pGraph->rootGenerator->id, pGraph->rootGenerator);
    {
        hkbNodeIterator nodesOf(0, pGraph->rootGenerator);
        while (hkbNode* pNode = nodesOf.Next())
            templates.emplace(pNode->id, pNode);
    }

    StateHooks hooks;
    hooks.Templates = &templates;
    std::vector<std::unique_ptr<hkbNodeInternalStateInfo>> infos;
    std::vector<hkbNodeInternalStateInfo*> list;
    std::vector<const GraphState::Node*> runtime;
    uint32_t unmatched = 0, otherNode = 0, notFitting = 0, states = 0;
    for (const auto& cNode : nodes)
    {
        const auto it = templates.find(cNode.Id);
        if (it == templates.end())
        {
            ++unmatched;
            continue;
        }
        hkbNode& node = *it->second;
        if (CheckOf(node) != cNode.Check)
        {
            ++otherNode;
            continue;
        }
        auto pInfo = std::make_unique<hkbNodeInternalStateInfo>();
        pInfo->nodeId = int16_t(cNode.Id);
        std::memcpy(pInfo->syncInfo, &cNode.Sync, sizeof(pInfo->syncInfo));
        pInfo->hasActivateBeenCalled = cNode.Activated;
        const auto* pClass = cNode.State ? StateOf(ClassName(node)) : nullptr;
        if (hkReferencedObject* pState = pClass ? node.CreateInternalState() : nullptr)
        {
            hooks.Machine = ClassName(node) == "hkbStateMachine" ? static_cast<const hkbStateMachineTransitions*>(&node) : nullptr;
            hooks.Waiting.clear();
            auto reader = ReaderOf(*cNode.State);
            if (NodeMemory::Read(reader, reinterpret_cast<uint8_t*>(pState), *pClass, &hooks))
            {
                hooks.Resolve();
                pInfo->internalState = pState;
                hooks.States.push_back(pState);
                ++states;
            }
            else
            {
                hooks.Waiting.clear();
                RemoveReference(pState);
                ++notFitting;
            }
        }
        if (!cNode.Runtime.empty())
            runtime.push_back(&cNode);
        list.push_back(pInfo.get());
        infos.push_back(std::move(pInfo));
    }

    // Deactivated, its variable set kept (released with discard-when-inactive, 58331): the owner's words in it.
    auto* pSet = pBehaviour->animationVariables;
    AddReference(pSet);
    reinterpret_cast<hkbNode*>(pBehaviour)->Deactivate(aContext);
    if (!pBehaviour->animationVariables)
        pBehaviour->animationVariables = pSet;
    else
        RemoveReference(pSet);
    auto* pWords = pBehaviour->animationVariables;
    std::copy_n(aWords.begin(), std::min<size_t>(aWords.size(), pWords->size), pWords->data);

    // Activated with each node's state: a machine's restored state, a blender's restored children (59326).
    using TSetState = void(void*, hkEventContext*, const hkbBehaviorGraphInternalState*);
    POINTER_SKYRIMSE(TSetState, s_setInternalState, 58373);
    hkbBehaviorGraphInternalState state{};
    state.nodeInternalStateInfos = {list.data(), int32_t(list.size()), int32_t(list.size()) | int32_t(0x80000000)};
    state.variableValueSet = pWords; // copied onto itself
    s_restoring = true;
    s_setInternalState.Get()(pBehaviour, &aContext, &state);
    s_restoring = false;
    // The events its deactivation and its nodes' activations raised (start states entered before their states were
    // set, 59365): its owner's queue was empty after its update.
    int32_t dropped = 0;
    if (auto* pQueue = aGraph.character.eventQueue)
    {
        dropped = pQueue->count;
        pQueue->head = pQueue->tail = pQueue->count = 0;
    }

    // Readout: its owner's nodes not active after the restore, its machines not in their restored state.
    std::unordered_map<uint16_t, hkbNode*> clones;
    if (pGraph->activeNodes)
        for (int32_t i = 0; i < pGraph->activeNodes->size; ++i)
            if (hkbNode* pClone = pGraph->activeNodes->data[i].nodeClone)
                clones.emplace(pClone->id, pClone);
    uint32_t inactive = 0, otherState = 0;
    for (const auto* pInfo : list)
    {
        const auto it = clones.find(uint16_t(pInfo->nodeId));
        if (it == clones.end())
            ++inactive;
        else if (pInfo->internalState && ClassName(*it->second) == "hkbStateMachine" &&
                 static_cast<const hkbStateMachineNode*>(it->second)->currentStateId !=
                     *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(pInfo->internalState) + 0x58)) // its state's currentStateId
            ++otherState;
    }
    for (auto** ppSlot : hooks.Slots)
        *ppSlot = nullptr;
    for (auto* pState : hooks.States)
        RemoveReference(pState);

    // The nodes' plain members, onto their new clones.
    uint32_t runtimes = 0;
    for (const auto* pNode : runtime)
    {
        const auto it = clones.find(pNode->Id);
        const auto* pRuntime = it != clones.end() && CheckOf(*it->second) == pNode->Check ? RuntimeOf(it->second->GetClassType()) : nullptr;
        auto reader = ReaderOf(pNode->Runtime);
        runtimes += pRuntime && NodeMemory::Read(reader, reinterpret_cast<uint8_t*>(it->second), *pRuntime);
    }
    spdlog::debug("Graph stream: {:X} restored {} of {} nodes with a state, {} runtime of {} (no node {}, other node {}, not fitting {}; "
                  "transition effects {}, lost {}); then {} active, {} of its owner's not, {} machines in another state; {} events dropped; clips {}",
                  aGraph.holder ? aGraph.holder->formID : 0, states, nodes.size(), runtimes, runtime.size(), unmatched, otherNode, notFitting,
                  hooks.Effects, hooks.LostEffects, clones.size(), inactive, otherState, dropped, ClipTimes(*pGraph));
    return true;
}
} // namespace GraphInternalState
