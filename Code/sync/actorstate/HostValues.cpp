#include <actorstate/HostValues.h>

#include <characters/ActorPools.h>
#include <characters/HostCharacters.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>
#include <Messages/Relayed.h>

namespace
{
void Keep(std::map<uint32_t, float>& aKept, const std::map<uint32_t, float>& acValues) noexcept
{
    for (const auto& [cKey, cValue] : acValues)
        aKept[cKey] = cValue;
}
} // namespace

HostValues::HostValues(HostRouter& aRouter, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept
    : m_players(acPlayers)
    , m_characters(aCharacters)
{
    using enum HostRouter::Delivery;
    aRouter.Relay<RequestActorValueChanges, NotifyActorValueChanges>(FromOwner, &RequestActorValueChanges::Id);
    aRouter.Relay<RequestActorMaxValueChanges, NotifyActorMaxValueChanges>(FromOwner, &RequestActorMaxValueChanges::Id);
    aRouter.Relay<RequestDeathStateChange, NotifyDeathStateChange>(FromOwner, &RequestDeathStateChange::Id);
    aRouter.Relay<RequestHealthChangeBroadcast, NotifyHealthChangeBroadcast>(ToOwner, &RequestHealthChangeBroadcast::Id);
    aRouter.On(*this, &HostValues::OnPerks);
    aRouter.Watch(*this, &HostValues::OnValues);
    aRouter.Watch(*this, &HostValues::OnMaxValues);
}

void HostValues::OnValues(const RequestActorValueChanges& acRequest, ConnectionId_t aSender) noexcept
{
    auto* pRecord = m_characters.PlayerRecordOf(acRequest.Id, aSender);
    if (!pRecord)
        return;
    Keep(pRecord->InitialActorValues.ActorValuesList, acRequest.Values);
    auto& temporary = pRecord->InitialActorValues.ActorTemporaryValuesList;
    for (const auto& [cIndex, cValue] : acRequest.TemporaryValues)
        if (ActorPools::IsPool(cIndex))
            temporary[cIndex] = cValue;
}

void HostValues::OnMaxValues(const RequestActorMaxValueChanges& acRequest, ConnectionId_t aSender) noexcept
{
    if (auto* pRecord = m_characters.PlayerRecordOf(acRequest.Id, aSender))
        Keep(pRecord->InitialActorValues.ActorMaxValuesList, acRequest.Values);
}

void HostValues::OnPerks(const ActorPerksRequest& acRequest, ConnectionId_t aSender) noexcept
{
    auto* pRecord = m_characters.PlayerRecordOf(acRequest.SessionId, aSender);
    if (!pRecord)
        return;
    pRecord->PerksContent = acRequest.Content;
    NotifyActorPerks notify;
    static_cast<Payloads::ActorPerks&>(notify) = acRequest;
    m_players.SendToOthers(notify, aSender);
}
