#include <Services/Host/HostRecords.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/JoinMessages.h>
#include <Messages/Relayed.h>

HostRecords::HostRecords(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostRecords::OnPlayerRecord);
    aAuthority.Watch(*this, &HostRecords::OnPartyCall);
}

void HostRecords::OnPartyCall(const RoutedCallRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender != m_players.Host() || acRequest.To != CallTo::kOthers || !m_game.StoreStorySpell)
        return;
    const auto& cCall = acRequest.Call;
    const auto* cpSelf = std::get_if<ScriptForm>(&cCall.Self);
    if (cCall.Class != "Actor" || !cpSelf || cpSelf->SessionId || cpSelf->Form != GameId(0, 0x14) ||
        cCall.Arguments.empty() || (cCall.Function != "AddSpell" && cCall.Function != "RemoveSpell"))
        return;
    const auto* cpSpell = std::get_if<ScriptForm>(&cCall.Arguments[0]);
    if (cpSpell && !cpSpell->SessionId && cpSpell->Form)
        m_game.StoreStorySpell(cpSpell->Form, cCall.Function == "AddSpell");
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
    m_players.ForEach([&](ConnectionId_t aConnection, const HostPlayers::Player&) { StorePlace(aConnection); });
}

void HostRecords::OnPlayerLeft(ConnectionId_t aPlayer) const noexcept
{
    StorePlace(aPlayer);
}

void HostRecords::StorePlace(ConnectionId_t aPlayer) const noexcept
{
    const auto* cpPlayer = m_players.Find(aPlayer);
    if (!m_game.StorePlace || !cpPlayer || cpPlayer->IsHost || cpPlayer->Key.empty())
        return;
    if (const auto cPlace = m_characters.CharacterPlace(m_characters.PlayerCharacterOf(aPlayer)))
        m_game.StorePlace(cpPlayer->Key, cPlace->WorldSpaceId, cPlace->CellId, {cPlace->Position.x, cPlace->Position.y, cPlace->Position.z});
}
