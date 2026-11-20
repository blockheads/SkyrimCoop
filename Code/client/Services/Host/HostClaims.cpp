#include <Services/Host/HostClaims.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>

HostClaims::HostClaims(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(aCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostClaims::OnGiveBack);
}

bool HostClaims::Claim(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId, bool aDeathOnGiveBack, bool aTakeFrom) noexcept
{
    if (!m_characters.OwnerOf(aActorId) || m_characters.IsPlayerCharacter(aActorId) || Returning(aActorId) ||
        (m_game.Claimable && !m_game.Claimable(aActorId)))
        return false;
    // The same claim again (a rider getting back on): held already. Anything else only takes it over by handover.
    const auto cHold = m_holds.find(aActorId);
    if (cHold != m_holds.end() && cHold->second.To == aTo && cHold->second.ForId == aForId && cHold->second.DeathOnGiveBack == aDeathOnGiveBack)
        return true;
    if (cHold != m_holds.end() && !aTakeFrom)
        return false;
    const Hold cNew{aTo, aForId, ++m_lastClaimId, aDeathOnGiveBack};
    m_holds[aActorId] = cNew;
    m_characters.SetOwner(aActorId, aTo);
    spdlog::info("HostClaims: {:x} holds {:x} for {:x} (claim {})", aTo, aActorId, aForId, cNew.ClaimId);
    NotifyActorClaim notify;
    notify.ActorId = aActorId;
    notify.ForId = aForId;
    notify.ClaimId = cNew.ClaimId;
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
    // A give-back of another claim (passed on to a passenger, or an older one of the same game) is not the holder's word.
    const auto cHold = m_holds.find(acRequest.ActorId);
    if (cHold == m_holds.end() || cHold->second.To != aSender || acRequest.ClaimId != cHold->second.ClaimId)
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
    notify.ClaimId = cHold->second.ClaimId;
    notify.ForId = cHold->second.ForId; // the killer of a killed one: whom the claim was for, never the sender's word
    notify.Killed = acGiveBack.Killed && cHold->second.DeathOnGiveBack;
    const uint32_t cForId = cHold->second.ForId;
    m_holds.erase(cHold);
    m_characters.SetOwner(aActorId, m_players.Host());
    m_returning.insert(aActorId);
    spdlog::info("HostClaims: {:x} is the host's again{}", aActorId, notify.Killed ? ", killed" : "");
    m_players.SendToAll(notify);
    if (OnEnded)
        OnEnded(aActorId, cForId);
}

void HostClaims::Forget(uint32_t aActorId) noexcept
{
    m_holds.erase(aActorId);
    m_returning.erase(aActorId);
}
