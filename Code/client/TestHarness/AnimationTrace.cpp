#include "AnimationTrace.h"

#include <Games/ActorExtension.h>
#include "EngineCalls.h"

#include <Actor.h>
#include <BSAnimationGraphManager.h>
#include <Games/Events.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbNode.h>
#include <Utils.h>
#include <Misc/BSFixedString.h>
#include <AI/Movement/MovementControllerNPC.h>
#include <AI/AIProcess.h>
#include <BSTimer.h>
#include <NetImmerse/NiNode.h>
#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Components.h>
#include <Systems/InterpolationSystem.h>

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

#include <atomic>
#include <mutex>
#include <RTTI.h>

namespace TestHarness
{
namespace
{
std::mutex s_lock;
nlohmann::json s_entries = nlohmann::json::array();
std::atomic<uint64_t> s_frame{0};

int64_t Now() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void Record(nlohmann::json aEntry)
{
    aEntry["t"] = Now();
    aEntry["f"] = s_frame.load();
    std::scoped_lock _{s_lock};
    s_entries.push_back(std::move(aEntry));
}

// Every graph listened to sends here; the sink outlives them all (static, and taken off each before it goes).
struct GraphEventSink final : BSTEventSink<BSAnimationGraphEvent>
{
    BSTEventResult OnEvent(const BSAnimationGraphEvent* apEvent, const EventDispatcher<BSAnimationGraphEvent>* apSender) override
    {
        if (!apEvent || !apEvent->holder)
            return BSTEventResult::kOk;
        const auto* pGraph = reinterpret_cast<const BShkbAnimationGraph*>(reinterpret_cast<const uint8_t*>(apSender) -
                                                                         offsetof(BShkbAnimationGraph, animationGraphEvents));
        Record({{"kind", "graph"},
                {"actor", apEvent->holder->formID},
                {"event", apEvent->tag.data ? apEvent->tag.data : ""},
                {"payload", apEvent->payload.data ? apEvent->payload.data : ""},
                {"project", pGraph->projectName ? pGraph->projectName : ""}});
        return BSTEventResult::kOk;
    }
};
GraphEventSink s_sink;

// The graphs whose input events are recorded: those listened to.
std::atomic<int> s_inputGraphCount{0};
const BShkbAnimationGraph* s_inputGraphs[16]{};

// The engine's frames that sent an event, as RVAs (scripts/re/id.py reads them).
nlohmann::json CallerRvas()
{
    void* frames[10];
    const USHORT cCount = RtlCaptureStackBackTrace(1, 10, frames, nullptr);
    const auto cBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    nlohmann::json rvas = nlohmann::json::array();
    for (USHORT i = 0; i < cCount; ++i)
    {
        const auto cAddress = reinterpret_cast<uintptr_t>(frames[i]);
        if (cAddress > cBase && cAddress - cBase < 0x4000000)
            rvas.push_back(std::format("{:X}", cAddress - cBase));
    }
    return rvas;
}

// BShkbAnimationGraph::SendEvent (63591): every input event a graph handles, whoever sends it (an action, the engine
// directly). Chained under the graph input stream's hook, which drops a copy's own on the stream.
using TSendGraphEvent = bool(BShkbAnimationGraph*, const BSFixedString*);
TSendGraphEvent* RealSendGraphEvent = nullptr;

bool HookSendGraphEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent)
{
    const bool cTaken = RealSendGraphEvent(apGraph, apEvent);
    const int cCount = s_inputGraphCount.load(std::memory_order_acquire);
    for (int i = 0; i < cCount; ++i)
        if (s_inputGraphs[i] == apGraph)
        {
            Record({{"kind", "input"},
                    {"actor", apGraph->holder ? apGraph->holder->formID : 0},
                    {"event", apEvent && apEvent->data ? apEvent->data : ""},
                    {"taken", cTaken},
                    {"from", CallerRvas()}});
            break;
        }
    return cTaken;
}

bool s_inputHookInstalled = false;

Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}

// The clips a graph plays now, by node name: its active nodes, and those of the sub-behaviours among them.
void ActiveClips(const hkbBehaviorGraphNode* apGraph, std::vector<std::string>& aClips, nlohmann::json& aDetail, nlohmann::json& aRandom, int aDepth = 0)
{
    if (!apGraph || !apGraph->activeNodes || aDepth > 8)
        return;
    if (apGraph->random)
        aRandom["generators"][std::format("{:X}", reinterpret_cast<uintptr_t>(apGraph->random))] = apGraph->random->current;
    const auto& cNodes = *apGraph->activeNodes;
    for (int32_t i = 0; i < cNodes.size; ++i)
    {
        const hkbNode* pNode = cNodes.data[i].nodeClone;
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (!pClass || !pClass->name)
            continue;
        const std::string_view cClass = pClass->name;
        if (cClass == "hkbBehaviorReferenceGenerator")
            ActiveClips(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aClips, aDetail, aRandom, aDepth + 1);
        else if (cClass == "BSEventEveryNEventsModifier" && pNode->name)
        {
            const auto* pEvery = static_cast<const BSEventEveryNEventsModifier*>(pNode);
            aRandom["every_n"][pNode->name] = {pEvery->numberOfEventsSeen, pEvery->calculatedNumberOfEventsBefore};
        }
        else if (cClass.ends_with("ClipGenerator") && pNode->name)
        {
            aClips.emplace_back(pNode->name);
            if (cClass == "hkbClipGenerator")
            {
                const auto* pClip = static_cast<const hkbClipGenerator*>(pNode);
                const auto* pBinding = pClip->animationControl ? pClip->animationControl->binding : nullptr;
                const hkaAnimation* pAnimation = pBinding ? pBinding->animation : nullptr;
                aDetail[pNode->name] = {{"local_time", pClip->localTime}, {"loaded", pAnimation != nullptr},
                                        {"duration", pAnimation ? pAnimation->duration : 0.f}, {"at_end", pClip->atEnd}};
            }
        }
    }
}

std::vector<std::string> ActiveClips(Actor& aActor, nlohmann::json& aDetail, nlohmann::json& aRandom)
{
    std::vector<std::string> clips;
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(aActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return clips;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        if (pManager->animationGraphIndex < pManager->animationGraphs.size)
            if (const BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(pManager->animationGraphIndex))
                ActiveClips(reinterpret_cast<const hkbBehaviorGraphNode*>(pGraph->behaviorGraph), clips, aDetail, aRandom);
    }
    pManager->Release();
    std::ranges::sort(clips);
    return clips;
}
} // namespace

void AnimationTrace::Watch(const std::vector<uint32_t>& acActors, const std::vector<std::string>& acGraphBools,
                           const std::vector<std::string>& acGraphFloats, const std::vector<std::string>& acGraphInts)
{
    for (const auto& cName : acGraphInts)
        if (std::find(m_graphInts.begin(), m_graphInts.end(), cName) == m_graphInts.end())
            m_graphInts.push_back(cName);
    for (const auto& cName : acGraphFloats)
        if (std::find(m_graphFloats.begin(), m_graphFloats.end(), cName) == m_graphFloats.end())
            m_graphFloats.push_back(cName);
    for (const uint32_t cId : acActors)
        m_actors.try_emplace(cId);
    for (const auto& cName : acGraphBools)
        if (std::find(m_graphBools.begin(), m_graphBools.end(), cName) == m_graphBools.end())
            m_graphBools.push_back(cName);
    Record({{"kind", "watch"}, {"actors", acActors}});
    Update();
}

nlohmann::json AnimationTrace::Take()
{
    std::scoped_lock _{s_lock};
    return std::exchange(s_entries, nlohmann::json::array());
}

nlohmann::json AnimationTrace::Stop()
{
    s_inputGraphCount.store(0, std::memory_order_release);
    for (BShkbAnimationGraph* pGraph : m_graphs)
    {
        pGraph->animationGraphEvents.UnRegisterSink(&s_sink);
        if (InterlockedDecrement(&pGraph->refCount) == 0)
            delete pGraph;
    }
    m_graphs.clear();
    m_actors.clear();
    m_graphBools.clear();
    m_graphFloats.clear();
    m_graphInts.clear();
    m_game = nullptr;
    return Take();
}

void AnimationTrace::Poll()
{
    // Chained under the stream's 63591 hook, once the hooks are in.
    if (!std::exchange(s_inputHookInstalled, true))
    {
        Hooks::Resolve<63591>(&RealSendGraphEvent);
        if (!Hooks::FunctionHookManager::GetInstance().AddChained(Hooks::FunctionHook(
                reinterpret_cast<void**>(&RealSendGraphEvent), reinterpret_cast<void*>(HookSendGraphEvent), "HookSendGraphEvent")))
            spdlog::warn("AnimationTrace: could not chain under the graph event hook");
    }
    ++s_frame;
    Update();
}

void AnimationTrace::Update()
{
    // The game's own: a kill cam, and how fast its clock runs.
    nlohmann::json game = {{"kill_cam", EngineCalls::IsKillCamPlaying()}, {"time", std::round(EngineCalls::GetGlobalTimeMultiplier() * 100.f) / 100.f}};
    if (game != m_game)
    {
        Record({{"kind", "game"}, {"actor", 0}, {"state", game}});
        m_game = std::move(game);
    }
    for (auto& [id, watched] : m_actors)
    {
        ListenToGraphs(id);
        Sample(id, watched);
    }
}

void AnimationTrace::ListenToGraphs(uint32_t aFormId)
{
    Actor* pActor = GetActor(aFormId);
    BSAnimationGraphManager* pManager = nullptr;
    if (!pActor || !EngineVirtual(pActor->animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t i = 0; i < pManager->animationGraphs.size; ++i)
        {
            BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(i);
            if (!pGraph || std::find(m_graphs.begin(), m_graphs.end(), pGraph) != m_graphs.end())
                continue;
            InterlockedIncrement(&pGraph->refCount);
            pGraph->animationGraphEvents.RegisterSink(&s_sink);
            m_graphs.push_back(pGraph);
            if (const int cCount = s_inputGraphCount.load(); cCount < static_cast<int>(std::size(s_inputGraphs)))
            {
                s_inputGraphs[cCount] = pGraph;
                s_inputGraphCount.store(cCount + 1, std::memory_order_release);
            }
            Record({{"kind", "listen"}, {"actor", aFormId}, {"project", pGraph->projectName ? pGraph->projectName : ""}});
        }
    }
    pManager->Release();
}

void AnimationTrace::Sample(uint32_t aFormId, Watched& aWatched)
{
    Actor* pActor = GetActor(aFormId);
    auto& actors = World::Get().GetActorBridge();
    if (!pActor)
    {
        if (!aWatched.State.is_null())
            Record({{"kind", "gone"}, {"actor", aFormId}});
        aWatched.State = nullptr;
        return;
    }

    // An action the graph took (an owner's own, a copy's replay), as the mod last recorded it.
    const ActionEvent cLatest = actors.GetActorLatestAnimation(aFormId);
    std::string action = std::format("{:X} '{}' idle {:X} target {:X} tick {}", cLatest.ActionId, cLatest.EventName.c_str(),
                                     cLatest.IdleId, cLatest.TargetId, cLatest.Tick);
    if (action != aWatched.Action)
    {
        if (!aWatched.Action.empty())
            Record({{"kind", "action"}, {"actor", aFormId}, {"action", cLatest.ActionId}, {"event", cLatest.EventName.c_str()},
                    {"idle", cLatest.IdleId}, {"target", cLatest.TargetId}});
        aWatched.Action = std::move(action);
    }

    nlohmann::json clipDetail = nlohmann::json::object();
    nlohmann::json random = nlohmann::json::object();
    auto clips = ActiveClips(*pActor, clipDetail, random);
    // Also when a clip's file arrives or it ends, its name unchanged: a clip started without one ends in a frame.
    nlohmann::json status = nlohmann::json::object();
    for (const auto& [cName, cDetail] : clipDetail.items())
        status[cName] = {cDetail["loaded"], cDetail["at_end"]};
    status["random"] = random;
    if (clips != aWatched.Clips || status != aWatched.ClipStatus)
    {
        Record({{"kind", "clips"}, {"actor", aFormId}, {"clips", clips}, {"detail", clipDetail}, {"random", random}});
        aWatched.Clips = std::move(clips);
        aWatched.ClipStatus = std::move(status);
    }

    nlohmann::json state = {{"graph_updates", (pActor->flags2 & Actor::ActorFlags::SHOULD_ANIM_GRAPH_UPDATE) != 0},
                            {"every_frame", (pActor->flags2 & Actor::kForceAnimGraphUpdate) != 0},
                            {"life", ActorState::LifeStateOf(pActor->actorState.flags1)},
                            {"weapon", pActor->actorState.GetWeaponState()},
                            {"kill_move", EngineCalls::IsInKillMove(aFormId)},
                            {"health", std::round(actors.GetActorValue(aFormId, 24) * 10.f) / 10.f}};
    for (const auto& cName : m_graphBools)
        state[cName] = EngineCalls::GetGraphBool(aFormId, cName.c_str());
    for (const auto& cName : m_graphInts)
        state[cName] = EngineCalls::GetGraphInt(aFormId, cName.c_str());
    if (state != aWatched.State)
    {
        Record({{"kind", "state"}, {"actor", aFormId}, {"state", state}});
        aWatched.State = std::move(state);
    }

    float position[3]{}, rotation[3]{};
    actors.GetActorPosition(aFormId, position[0], position[1], position[2]);
    actors.GetActorRotation(aFormId, rotation[0], rotation[1], rotation[2]);
    nlohmann::json entry = {{"kind", "pos"}, {"actor", aFormId}, {"pos", {position[0], position[1], position[2]}}, {"yaw", rotation[2]}};
    entry["sit"] = pActor->actorState.GetSitSleepState();
    // The engine's frame (BSTimer's realTimeDelta, ms): per-frame speeds without our update's uneven wall clock (S10).
    if (const BSTimer* pTimer = BSTimer::Get())
        entry["dt"] = std::round(pTimer->RealTimeDelta * 1e5f) / 100.f;
    if (const auto cController = EngineCalls::GetController(aFormId))
        entry["controller"] = {{"flags", cController->Flags}, {"filter", cController->Filter}, {"state", cController->State},
                               {"at", cController->Position}, {"step", cController->StepVelocity}};
    // The drawn body: the 3D's root and feet (world), what GaitWatch reads against the actor's place.
    if (NiNode* p3D = pActor->GetNiNode())
    {
        static const BSFixedString s_feet[2] = {BSFixedString("NPC L Foot [Lft ]"), BSFixedString("NPC R Foot [Rft ]")};
        entry["root"] = {p3D->world.translate.x, p3D->world.translate.y, p3D->world.translate.z};
        for (const auto& cFoot : s_feet)
            if (const NiAVObject* pFoot = p3D->GetByName(cFoot))
                entry["feet"].push_back({pFoot->world.translate.x, pFoot->world.translate.y, pFoot->world.translate.z});
    }
    // Who moves it: the graph (animation-driven) or the controller, by the Virtual Path Follower without a graph.
    if (const auto* pMovement = reinterpret_cast<const MovementControllerNPC*>(pActor->movementDrivenControl))
        entry["movement"] = {{"state", pMovement->movementState}, {"loaded_agents", pMovement->loadedAgents}};
    if (pActor->currentProcess)
        entry["process_level"] = pActor->currentProcess->processLevel;
    if (const auto cSeen = EngineCalls::GetPoseVisibility(aFormId))
        entry["cull"] = {cSeen->WasInFrustum, cSeen->CullingState, cSeen->Drawn};
    if (const auto* pExtension = pActor->GetExtension(); pExtension && pExtension->HasCopyGoal)
        entry["goal"] = {pExtension->CopyGoal[0], pExtension->CopyGoal[1], pExtension->CopyGoal[2]};
    for (const auto& cName : m_graphFloats)
        entry["floats"][cName] = EngineCalls::GetGraphFloat(aFormId, cName.c_str());
    // A copy's clocks: the owner's time it shows, the game's copy clock and the session's time as read.
    auto& world = World::Get();
    for (auto&& [entity, form, interpolation] : world.view<FormIdComponent, InterpolationComponent>().each())
        if (form.Id == aFormId)
        {
            entry["playback"] = InterpolationSystem::PlaybackTick(world, entity);
            entry["clock"] = world.GetCopyClock().Now();
            entry["session"] = world.GetTick();
        }
    Record(std::move(entry));
}
} // namespace TestHarness
