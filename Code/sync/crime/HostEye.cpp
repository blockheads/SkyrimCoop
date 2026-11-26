#include <crime/HostEye.h>

#include <characters/HostCharacters.h>
#include <crime/HostCrimeGame.h>
#include <core/HostPlayers.h>

#include <Messages/NotifyStealthEye.h>

HostEye::HostEye(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostCrimeGame& acGame) noexcept
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
        // Its copy not loaded here: no NPC of the host's sees it.
        const DetectionRules::EyeReading cEye = m_game.ReadEye(cCharacter).value_or(DetectionRules::EyeReading{});
        const auto cSent = m_sent.find(aPlayer);
        if (cSent == m_sent.end() ? cEye == DetectionRules::EyeReading{} : !DetectionRules::EyeChanged(cSent->second, cEye))
            return;
        m_sent[aPlayer] = cEye;
        NotifyStealthEye notify;
        notify.Level = cEye.Level;
        notify.Seeing = cEye.Seeing;
        notify.Stealth = cEye.Stealth;
        m_players.SendTo(aPlayer, notify);
    });
}
