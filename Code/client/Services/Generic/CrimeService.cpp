#include <Events/DisconnectedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/UpdateEvent.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Components/RemoteComponent.h>
#include <Services/CrimeService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <Diagnostics/Profile.h>

CrimeService::CrimeService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                           ICrimeBridge& aBridge) noexcept
    : m_world(aWorld), m_transport(aTransport), m_bridge(aBridge)
{
    Listen<NotifyCrimeState, &CrimeService::OnState>(m_connections, aDispatcher, this);
    Listen<NotifyLockCrime, &CrimeService::OnLockCrime>(m_connections, aDispatcher, this);
    Listen<NotifyCrimeConfiscation, &CrimeService::OnConfiscation>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &CrimeService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &CrimeService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<GameLoadedEvent, &CrimeService::OnLoaded>(m_connections, aDispatcher, this);
}

CrimeService::~CrimeService() noexcept
{
    m_bridge.SetRole(ICrimeBridge::Role::Offline);
}

bool CrimeService::Judge(uint32_t aActorSessionId, const CrimeRequest& aRequest) noexcept
{
    const bool cTheft = aRequest.Action == CrimeAction::Theft || aRequest.Action == CrimeAction::ContainerTheft;
    // The lock menu needs its answer now; anything else keeps its place behind a held theft.
    if (aRequest.Action == CrimeAction::Lockpick || (!cTheft && m_held.empty()))
        return m_bridge.Judge(Utils::GetFormIdBySessionId(aActorSessionId), aRequest);
    m_held.push_back({aActorSessionId, aRequest, 0.0, 0});
    if (cTheft)
        m_bridge.AskWitnesses(Utils::GetFormIdBySessionId(aActorSessionId));
    return false;
}

void CrimeService::JudgeHeld(double aDelta) noexcept
{
    // Asked each frame a witness's state stayed under 0.3 s old (crime.md), so all have answered by then.
    constexpr double cWitnessWait = 0.5;
    while (!m_held.empty())
    {
        auto& held = m_held.front();
        const auto cActor = Utils::GetFormIdBySessionId(held.ActorSessionId);
        const bool cTheft = held.Request.Action == CrimeAction::Theft || held.Request.Action == CrimeAction::ContainerTheft;
        if (cActor && cTheft && (held.Asks < 2 || held.Waited < cWitnessWait))
        {
            m_bridge.AskWitnesses(cActor);
            ++held.Asks;
            held.Waited += aDelta;
            return;
        }
        if (cActor) // a guest gone meanwhile has no copy to judge
            m_bridge.Judge(cActor, held.Request);
        m_held.erase(m_held.begin());
    }
}

void CrimeService::OnLockCrime(const NotifyLockCrime& aState) noexcept
{
    m_bridge.LockCaught(aState.Attempt);
}

std::vector<CrimeConfiscation> CrimeService::TakeConfiscations() noexcept
{
    auto due = m_bridge.TakeConfiscations();
    std::erase_if(due, [](CrimeConfiscation& aDue) {
        auto actor = Utils::GetSessionIdByFormId(aDue.Actor);
        if (!actor)
            return true;
        aDue.Actor = *actor;
        return false;
    });
    return due;
}

void CrimeService::OnConfiscation(const NotifyCrimeConfiscation& aDue) noexcept
{
    if (aDue.Id && aDue.Id > m_lastConfiscation)
    {
        m_lastConfiscation = aDue.Id;
        m_bridge.Confiscate(aDue.Faction);
    }
}

void CrimeService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("CrimeService::OnUpdate");
    const auto role = !m_transport.IsOnline()            ? ICrimeBridge::Role::Offline
                      : m_transport.IsConnectedLocally() ? ICrimeBridge::Role::Host
                                                         : ICrimeBridge::Role::Guest;
    m_bridge.SetRole(role);
    if (role == ICrimeBridge::Role::Host)
    {
        std::vector<uint32_t> copies;
        for (auto entity : m_world.view<RemoteComponent, PlayerComponent, FormIdComponent>())
            copies.push_back(m_world.get<FormIdComponent>(entity).Id);
        m_bridge.UpdatePlayers(copies);
        JudgeHeld(acEvent.Delta);
    }
    for (auto& request : m_bridge.TakeActions())
    {
        request.Sequence = ++m_sequence;
        m_transport.Send(request);
    }
    // Guest-native events or console commands cannot override the host's latest snapshot.
    if (role == ICrimeBridge::Role::Guest && m_hasState && m_bridge.ReadGold() != m_gold)
        m_bridge.ApplyGold(m_gold);
}

void CrimeService::OnState(const NotifyCrimeState& aState) noexcept
{
    if (m_transport.IsConnectedLocally())
        return;
    m_gold = aState.Gold;
    m_hasState = true;
    m_bridge.ApplyGold(m_gold);
}

void CrimeService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_bridge.SetRole(ICrimeBridge::Role::Offline);
    m_sequence = 0;
    m_lastConfiscation = 0;
    m_hasState = false;
    m_gold.clear();
    m_held.clear();
}

void CrimeService::OnLoaded(const GameLoadedEvent&) noexcept
{
    // The join's fresh snapshot will follow the loaded host save.
    m_hasState = false;
    m_gold.clear();
    m_held.clear();
}
