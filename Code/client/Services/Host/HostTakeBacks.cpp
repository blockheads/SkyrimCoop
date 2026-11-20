#include <Services/Host/HostTakeBacks.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/RemoteComponent.h>
#include <Events/UpdateEvent.h>
#include <Games/Animation/GraphStream.h>
#include <Games/Animation/ReplayState.h>
#include <Messages/Relayed.h>
#include <Services/CharacterService.h>
#include <SessionIds.h>
#include <World.h>

HostTakeBacks::HostTakeBacks(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors, std::function<void(uint32_t)> aTaken) noexcept
    : m_world(aWorld)
    , m_actors(aActors)
    , m_taken(std::move(aTaken))
{
    Listen<NotifyActorGiveBack, &HostTakeBacks::OnGiveBack>(m_connections, aDispatcher, this);
}

void HostTakeBacks::OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept
{
    m_takeBacks.push_back({acMessage.ActorId, acMessage.Tick, acMessage.Whereabouts, acMessage.Killed ? acMessage.ForId : 0,
                           m_world.GetTick() + ReplayState::cPoseHoldMs});
}

void HostTakeBacks::Update(const UpdateEvent&) noexcept
{
    std::erase_if(m_takeBacks, [this](const TakeBack& acTakeBack) { return Take(acTakeBack); });
}

bool HostTakeBacks::Take(const TakeBack& acTakeBack) noexcept
{
    entt::entity entity = entt::null;
    for (const auto cEntity : m_world.view<FormIdComponent>())
        if (Utils::GetSessionId(cEntity) == acTakeBack.SessionId)
            entity = cEntity;
    // Gone, or ours all along (the host's own ride): nothing to take.
    if (entity == entt::null || !m_world.all_of<RemoteComponent>(entity))
    {
        m_taken(acTakeBack.SessionId);
        return true;
    }
    const uint32_t cFormId = m_world.get<FormIdComponent>(entity).Id;
    if (acTakeBack.KillerId && !m_actors.ActorKillCounted(cFormId) && m_world.GetTick() < acTakeBack.Until)
        return false;
    const auto cDone = m_world.GetGraphStreams().StreamDoneThrough(cFormId);
    if (!ReplayState::TakesOverMount(acTakeBack.Tick, cDone.has_value(), cDone.value_or(0)))
        return false;
    auto& characters = m_world.GetCharacterService();
    if (characters.TakeOwnership(cFormId, acTakeBack.SessionId, entity))
        spdlog::info("Actor {:X}: taken back, its last holder's ops run through {}", cFormId, acTakeBack.Tick);
    characters.PlaceAt(cFormId, acTakeBack.Whereabouts);
    // Its holder's clip killed it there under the hold: its death is this game's (KillImpl 37896).
    if (acTakeBack.KillerId)
        m_actors.KillActorBy(cFormId, Utils::GetFormIdBySessionId(acTakeBack.KillerId));
    m_taken(acTakeBack.SessionId);
    return true;
}
