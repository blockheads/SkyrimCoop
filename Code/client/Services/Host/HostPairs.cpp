#include <Services/Host/HostPairs.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>

HostPairs::HostPairs(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(aCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostPairs::OnClaim);
    aAuthority.On(*this, &HostPairs::OnDone);
}

bool HostPairs::Claimed(uint32_t aSessionId) const noexcept
{
    return std::ranges::any_of(m_claims, [&](const Claim& acClaim) { return acClaim.ActorId == aSessionId || acClaim.PartnerId == aSessionId; });
}

void HostPairs::OnClaim(const PairClaimRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const ConnectionId_t cPartnerOwner = m_characters.OwnerOf(acRequest.PartnerId);
    if (m_characters.OwnerOf(acRequest.ActorId) != aSender || !cPartnerOwner || cPartnerOwner == aSender)
    {
        spdlog::warn("HostPairs: {:x} claimed {:x} with {:x}, which it cannot", aSender, acRequest.ActorId, acRequest.PartnerId);
        return;
    }
    const bool cPlayer = m_characters.IsPlayerCharacter(acRequest.PartnerId);
    uint8_t reason = Payloads::Pair::kNone;
    if (Claimed(acRequest.ActorId) || Claimed(acRequest.PartnerId))
        reason = Payloads::Pair::kCrossed;
    else if (!cPlayer && (cPartnerOwner != m_players.Host() || (m_game.CanPair && !m_game.CanPair(acRequest.PartnerId))))
        reason = Payloads::Pair::kCannotPair; // dead, its graph stopped, or another game's (a ridden mount)
    if (reason != Payloads::Pair::kNone)
    {
        spdlog::info("HostPairs: {:x}'s pair of {:x} with {:x} refused ({})", aSender, acRequest.ActorId, acRequest.PartnerId, reason);
        NotifyPairRefused refused;
        static_cast<Payloads::Pair&>(refused) = acRequest;
        refused.Reason = reason;
        m_players.SendToAll(refused);
        return;
    }
    const bool cLent = !cPlayer && m_characters.Lend(acRequest.PartnerId, aSender);
    m_claims.push_back({acRequest.ActorId, acRequest.PartnerId, aSender, cLent});
    spdlog::info("HostPairs: {:x} holds the pair of {:x} with {:x}{}", aSender, acRequest.ActorId, acRequest.PartnerId, cLent ? ", its game's for it" : "");
    NotifyPairClaim notify;
    static_cast<Payloads::Pair&>(notify) = acRequest;
    m_players.SendToAll(notify);
}

void HostPairs::OnDone(const PairDoneRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const auto cClaim = std::ranges::find_if(m_claims, [&](const Claim& acClaim) {
        return acClaim.Claimer == aSender && acClaim.ActorId == acRequest.ActorId && acClaim.PartnerId == acRequest.PartnerId;
    });
    if (cClaim == m_claims.end())
        return;
    if (cClaim->Lent)
        m_characters.Lend(cClaim->PartnerId, m_players.Host());
    m_claims.erase(cClaim);
    // The killer is the claim's own actor (KillActorHandler 42838 credits the pair's partner), never the sender's word.
    NotifyPairDone notify;
    static_cast<Payloads::Pair&>(notify) = acRequest;
    notify.Reason = Payloads::Pair::kNone;
    m_players.SendToAll(notify);
}

void HostPairs::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    const uint32_t cCharacter = m_characters.PlayerCharacterOf(aPlayer);
    std::erase_if(m_claims, [&](const Claim& acClaim) {
        if (acClaim.Claimer != aPlayer && acClaim.PartnerId != cCharacter)
            return false;
        // The other game ends the pair as the leaver's actor goes; a lent NPC is the host's again, at once.
        if (acClaim.Lent && acClaim.Claimer == aPlayer)
            m_characters.Lend(acClaim.PartnerId, m_players.Host());
        NotifyPairDone notify;
        notify.ActorId = acClaim.ActorId;
        notify.PartnerId = acClaim.PartnerId;
        m_players.SendToOthers(notify, aPlayer);
        return true;
    });
}
