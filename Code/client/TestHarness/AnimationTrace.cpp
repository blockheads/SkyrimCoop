#include "AnimationTrace.h"
#include "EngineCalls.h"

#include <Actor.h>
#include <BSAnimationGraphManager.h>
#include <Games/Events.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbNode.h>
#include <Utils.h>
#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Components.h>
#include <Systems/InterpolationSystem.h>

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

Actor* GetActor(uint32_t aFormId) noexcept
{
    return Cast<Actor>(TESForm::GetById(aFormId));
}

// The clips a graph plays now, by node name: its active nodes, and those of the sub-behaviours among them.
void ActiveClips(const hkbBehaviorGraphNode* apGraph, std::vector<std::string>& aClips, nlohmann::json& aDetail, int aDepth = 0)
{
    if (!apGraph || !apGraph->activeNodes || aDepth > 8)
        return;
    const auto& cNodes = *apGraph->activeNodes;
    for (int32_t i = 0; i < cNodes.size; ++i)
    {
        const hkbNode* pNode = cNodes.data[i].nodeClone;
        const hkClass* pClass = pNode ? pNode->GetClassType() : nullptr;
        if (!pClass || !pClass->name)
            continue;
        const std::string_view cClass = pClass->name;
        if (cClass == "hkbBehaviorReferenceGenerator")
            ActiveClips(static_cast<const hkbBehaviorReferenceGenerator*>(pNode)->behavior, aClips, aDetail, aDepth + 1);
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

std::vector<std::string> ActiveClips(Actor& aActor, nlohmann::json& aDetail)
{
    std::vector<std::string> clips;
    BSAnimationGraphManager* pManager = nullptr;
    if (!EngineVirtual(aActor.animationGraphHolder).GetBSAnimationGraph(&pManager) || !pManager)
        return clips;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        if (pManager->animationGraphIndex < pManager->animationGraphs.size)
            if (const BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(pManager->animationGraphIndex))
                ActiveClips(reinterpret_cast<const hkbBehaviorGraphNode*>(pGraph->behaviorGraph), clips, aDetail);
    }
    pManager->Release();
    std::ranges::sort(clips);
    return clips;
}
} // namespace

void AnimationTrace::Watch(const std::vector<uint32_t>& acActors, const std::vector<std::string>& acGraphBools,
                           const std::vector<std::string>& acGraphFloats)
{
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
    m_game = nullptr;
    return Take();
}

void AnimationTrace::Poll()
{
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
    if (auto clips = ActiveClips(*pActor, clipDetail); clips != aWatched.Clips)
    {
        Record({{"kind", "clips"}, {"actor", aFormId}, {"clips", clips}, {"detail", clipDetail}});
        aWatched.Clips = std::move(clips);
    }

    nlohmann::json state = {{"graph_updates", (pActor->flags2 & Actor::ActorFlags::SHOULD_ANIM_GRAPH_UPDATE) != 0},
                            {"life", ActorState::LifeStateOf(pActor->actorState.flags1)},
                            {"weapon", pActor->actorState.GetWeaponState()},
                            {"kill_move", EngineCalls::IsInKillMove(aFormId)},
                            {"health", std::round(actors.GetActorValue(aFormId, 24) * 10.f) / 10.f}};
    for (const auto& cName : m_graphBools)
        state[cName] = EngineCalls::GetGraphBool(aFormId, cName.c_str());
    if (state != aWatched.State)
    {
        Record({{"kind", "state"}, {"actor", aFormId}, {"state", state}});
        aWatched.State = std::move(state);
    }

    float position[3]{}, rotation[3]{};
    actors.GetActorPosition(aFormId, position[0], position[1], position[2]);
    actors.GetActorRotation(aFormId, rotation[0], rotation[1], rotation[2]);
    nlohmann::json entry = {{"kind", "pos"}, {"actor", aFormId}, {"pos", {position[0], position[1], position[2]}}, {"yaw", rotation[2]}};
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
