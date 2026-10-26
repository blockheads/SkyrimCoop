#include <Services/Host/HostRecords.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/JoinMessages.h>

HostRecords::HostRecords(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostRecords::OnPlayerRecord);
}

void HostRecords::OnPlayerRecord(const PlayerRecordRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const auto* cpPlayer = m_players.Find(aSender);
    if (!cpPlayer || cpPlayer->IsHost || cpPlayer->Key.empty() || cpPlayer->Key != acRequest.Record.Key)
    {
        spdlog::warn("HostRecords: {:x} sent a record under '{}', not its own key: dropped", aSender, acRequest.Record.Key);
        return;
    }
    spdlog::debug("HostRecords: {:x} '{}' keeps {} (level {}, {} items, {} carried)", aSender, acRequest.Record.Key, acRequest.Record.Name,
                  acRequest.Record.Level, acRequest.Record.Items.Entries.size(),
                  std::ranges::fold_left(acRequest.Record.Items.Entries, 0, [](int aSum, const auto& acEntry) { return aSum + acEntry.Count; }));
    if (m_game.StoreRecord)
        m_game.StoreRecord(acRequest.Record);
}

void HostRecords::OnSaving() const noexcept
{
    if (!m_game.StorePlace)
        return;
    m_players.ForEach([&](ConnectionId_t aConnection, const HostPlayers::Player& acPlayer) {
        if (acPlayer.IsHost || acPlayer.Key.empty())
            return;
        if (const auto cPlace = m_characters.CharacterPlace(m_characters.PlayerCharacterOf(aConnection)))
            m_game.StorePlace(acPlayer.Key, cPlace->WorldSpaceId, cPlace->CellId,
                              {cPlace->Position.x, cPlace->Position.y, cPlace->Position.z});
    });
}
