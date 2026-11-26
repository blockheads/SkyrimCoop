#include <characters/HostRecords.h>

#include <core/HostRouter.h>
#include <characters/HostCharacters.h>
#include <characters/HostCharactersGame.h>
#include <core/HostPlayers.h>

#include <Messages/JoinMessages.h>
#include <Messages/Relayed.h>

HostRecords::HostRecords(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostCharactersGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aRouter.On(*this, &HostRecords::OnPlayerRecord);
    aRouter.On(*this, &HostRecords::OnRecord);
    aRouter.On(*this, &HostRecords::OnRecordAsk);
    aRouter.Watch(*this, &HostRecords::OnPartyCall);
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
    if (const auto* cpPlayer = m_players.Find(aPlayer); cpPlayer && !cpPlayer->IsHost && !cpPlayer->Key.empty() && m_game.SettleRecord)
        m_game.SettleRecord(cpPlayer->Key);
}

void HostRecords::StorePlace(ConnectionId_t aPlayer) const noexcept
{
    const auto* cpPlayer = m_players.Find(aPlayer);
    if (!m_game.StorePlace || !cpPlayer || cpPlayer->IsHost || cpPlayer->Key.empty())
        return;
    if (const auto cPlace = m_characters.CharacterPlace(m_characters.PlayerCharacterOf(aPlayer)))
        m_game.StorePlace(cpPlayer->Key, cPlace->WorldSpaceId, cPlace->CellId, {cPlace->Position.x, cPlace->Position.y, cPlace->Position.z});
}

void HostRecords::OnRecord(const RecordRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (m_characters.OwnerOf(acRequest.Id) != aSender)
    {
        spdlog::debug("HostRecords: {:x} declared a part of {:x}, which it does not own: dropped", aSender, acRequest.Id);
        return;
    }
    if (!m_characters.RideGranted(acRequest.Id, acRequest.Part))
    {
        spdlog::info("HostRecords: {:x} declared a seat the host did not give it: dropped", acRequest.Id);
        return;
    }
    RecordPart& held = m_parts[acRequest.Id][acRequest.Part.Kind];
    if (acRequest.Part.Version <= held.Version)
        return;
    auto merged = MergedPart(held.Version ? &held : nullptr, acRequest.Part);
    if (!merged)
    {
        // A change on top of a version the host lacks: its owner sends the whole part (R3); nothing goes out.
        NotifyRecordAsk ask;
        ask.Id = acRequest.Id;
        ask.Kind = acRequest.Part.Kind;
        m_players.SendTo(aSender, ask);
        return;
    }
    held = {acRequest.Part.Kind, acRequest.Part.Version, std::move(*merged)};
    if (OnKept)
        OnKept(acRequest.Id, held);
    NotifyRecord notify;
    static_cast<Payloads::Record&>(notify) = acRequest;
    m_players.SendToOthers(notify, aSender, Net::kReliable);
}

void HostRecords::OnRecordAsk(const RecordAskRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const ConnectionId_t cOwner = m_characters.OwnerOf(acRequest.Id);
    if (!cOwner || cOwner == aSender)
        return;
    NotifyRecordAsk notify;
    static_cast<Payloads::RecordAsk&>(notify) = acRequest;
    m_players.SendTo(cOwner, notify);
}

void HostRecords::Announced(uint32_t aSessionId, const RecordParts& acParts) noexcept
{
    auto& parts = m_parts[aSessionId];
    parts.clear();
    for (const RecordPart& cPart : acParts.Parts)
        parts[cPart.Kind] = cPart;
}

RecordParts HostRecords::PartsOf(uint32_t aSessionId) const noexcept
{
    RecordParts parts;
    if (const auto it = m_parts.find(aSessionId); it != m_parts.end())
        for (const auto& [cKind, cPart] : it->second)
            if (cPart.Version)
                parts.Parts.push_back(cPart);
    return parts;
}
