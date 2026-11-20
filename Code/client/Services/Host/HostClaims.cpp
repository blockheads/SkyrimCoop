#include <Services/Host/HostClaims.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>

HostClaims::HostClaims(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept
    : m_players(acPlayers)
    , m_characters(aCharacters)
{
    aAuthority.On(*this, &HostClaims::OnGiveBack);
}

bool HostClaims::Claim(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId, bool aDeathOnGiveBack, bool aTakeFrom) noexcept
{
    if (!m_characters.OwnerOf(aActorId) || m_characters.IsPlayerCharacter(aActorId))
        return false;
    const auto cHold = m_holds.find(aActorId);
    if (cHold != m_holds.end() && cHold->second.To != aTo && !aTakeFrom)
        return false;
    if (cHold != m_holds.end() && cHold->second.To == aTo && cHold->second.ForId == aForId)
        return true;
    m_holds[aActorId] = {aTo, aForId};
    m_characters.SetOwner(aActorId, aTo);
    spdlog::info("HostClaims: {:x} holds {:x} for {:x}", aTo, aActorId, aForId);
    NotifyActorClaim notify;
    notify.ActorId = aActorId;
    notify.ForId = aForId;
    notify.DeathOnGiveBack = aDeathOnGiveBack;
    m_players.SendToAll(notify);
    return true;
}

HostClaims::ConnectionId_t HostClaims::HolderOf(uint32_t aActorId) const noexcept
{
    const auto cHold = m_holds.find(aActorId);
    return cHold == m_holds.end() ? 0 : cHold->second.To;
}

void HostClaims::OnGiveBack(const ActorGiveBackRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // A holder's late give-back after the claim passed on (a passenger drives on) is not the holder's word.
    if (HolderOf(acRequest.ActorId) != aSender)
        return;
    GiveBack(acRequest.ActorId, acRequest);
}

void HostClaims::Release(uint32_t aActorId) noexcept
{
    if (m_holds.contains(aActorId))
        GiveBack(aActorId, {});
}

void HostClaims::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    std::vector<uint32_t> held;
    for (const auto& [cActorId, cHold] : m_holds)
        if (cHold.To == aPlayer)
            held.push_back(cActorId);
    for (const uint32_t cActorId : held)
        Release(cActorId);
}

void HostClaims::GiveBack(uint32_t aActorId, const Payloads::ActorGiveBack& acGiveBack) noexcept
{
    const auto cHold = m_holds.find(aActorId);
    NotifyActorGiveBack notify;
    static_cast<Payloads::ActorGiveBack&>(notify) = acGiveBack;
    notify.ActorId = aActorId;
    notify.ForId = cHold->second.ForId; // the killer of a killed one: whom the claim was for, never the sender's word
    m_holds.erase(cHold);
    m_characters.SetOwner(aActorId, m_players.Host());
    spdlog::info("HostClaims: {:x} is the host's again{}", aActorId, notify.Killed ? ", killed" : "");
    m_players.SendToAll(notify);
}
