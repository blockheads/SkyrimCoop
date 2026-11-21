#include <Services/Host/HostEye.h>

#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/NotifyStealthEye.h>

HostEye::HostEye(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
}

void HostEye::Update() noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || !m_game.ReadEye)
        return;
    m_players.ForEach([&](ConnectionId_t aPlayer, const auto&) {
        const uint32_t cCharacter = m_characters.PlayerCharacterOf(aPlayer);
        if (aPlayer == cHost || !cCharacter)
            return;
        // Not here (its copy not loaded): the guest keeps the last.
        const auto cEye = m_game.ReadEye(cCharacter);
        if (!cEye)
            return;
        const auto cSent = m_sent.find(aPlayer);
        if (cSent != m_sent.end() && !DetectionRules::EyeChanged(cSent->second, *cEye))
            return;
        m_sent[aPlayer] = *cEye;
        NotifyStealthEye notify;
        notify.Level = cEye->Level;
        notify.Seeing = cEye->Seeing;
        notify.Stealth = cEye->Stealth;
        m_players.SendTo(aPlayer, notify);
    });
}
