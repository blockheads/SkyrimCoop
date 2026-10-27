#include "AnimationTrace.h"
#include "EngineCalls.h"

#include <Actor.h>
#include <BSAnimationGraphManager.h>
#include <Games/Events.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Utils.h>
#include <World.h>
#include <Bridges/IActorBridge.h>

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
} // namespace

void AnimationTrace::Watch(const std::vector<uint32_t>& acActors, const std::vector<std::string>& acGraphBools)
{
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
    Record({{"kind", "pos"}, {"actor", aFormId}, {"pos", {position[0], position[1], position[2]}}, {"yaw", rotation[2]}});
}
} // namespace TestHarness
