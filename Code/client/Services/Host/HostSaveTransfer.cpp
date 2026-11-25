#include <Services/Host/HostSaveTransfer.h>

#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/DeathMessages.h>

#include <algorithm>

HostSaveTransfer::HostSaveTransfer(HostNetwork& aNetwork, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_game(acGame)
{
}

void HostSaveTransfer::SetHeld(ConnectionId_t aConnection, std::vector<std::string> aSaves) noexcept
{
    // The same as before: what was sent stands.
    auto& held = m_held[aConnection];
    const bool cChanged = held != aSaves;
    held = std::move(aSaves);
    const auto it = m_receivers.find(aConnection);
    // It holds the save on its way ahead: no more of it.
    if (it != m_receivers.end() && it->second.Ahead && Holds(aConnection, it->second.Save))
        m_receivers.erase(it);
    // Sent a save to load it cannot use (its base gone, or told it held one it lost): against what it holds now.
    else if (const auto cMeant = m_recipients.find(aConnection); cChanged && cMeant != m_recipients.end() &&
             !Holds(aConnection, cMeant->second) && (it == m_receivers.end() || it->second.Ahead))
    {
        spdlog::info("HostSaveTransfer: {:x} lacks the save it was sent: sending it again", aConnection);
        m_receivers[aConnection] = {.Save = cMeant->second};
    }
}

void HostSaveTransfer::Want(ConnectionId_t aConnection) noexcept
{
    m_receivers[aConnection] = {};
}

bool HostSaveTransfer::Holds(ConnectionId_t aConnection, const std::string& acSave) const noexcept
{
    const auto it = m_held.find(aConnection);
    return it != m_held.end() && std::ranges::find(it->second, acSave) != it->second.end();
}

bool HostSaveTransfer::Empty() const noexcept
{
    return m_recipients.empty() && std::ranges::all_of(m_receivers, [](const auto& acReceiver) { return acReceiver.second.Ahead; });
}

bool HostSaveTransfer::IsSending(ConnectionId_t aConnection) const noexcept
{
    const auto it = m_receivers.find(aConnection);
    return it != m_receivers.end() && !it->second.Ahead;
}

void HostSaveTransfer::OnDisconnected(ConnectionId_t aConnection) noexcept
{
    m_receivers.erase(aConnection);
    m_held.erase(aConnection);
    m_recipients.erase(aConnection);
}

void HostSaveTransfer::Refuse(ConnectionId_t aConnection) noexcept
{
    AuthenticationResponse response;
    response.Type = AuthenticationResponse::ResponseType::kTransferFailed;
    m_network.Send(aConnection, response);
    m_network.Kick(aConnection);
}

void HostSaveTransfer::Offer(const std::string& acSave, const std::vector<ConnectionId_t>& acGuests) noexcept
{
    for (const ConnectionId_t cGuest : acGuests)
    {
        const auto it = m_receivers.find(cGuest);
        if (Holds(cGuest, acSave) || (it != m_receivers.end() && !it->second.Ahead))
            continue;
        m_receivers[cGuest] = {.Save = acSave, .Ahead = true};
    }
}

void HostSaveTransfer::Checkpoint(const std::string& acSave, const std::vector<ConnectionId_t>& acGuests) noexcept
{
    std::vector<ConnectionId_t> games = acGuests;
    // Games still joining: their save in flight, or all sent and not yet loading.
    for (const auto& [cConnection, cReceiver] : m_receivers)
        if (!cReceiver.Ahead && std::ranges::find(games, cConnection) == games.end())
            games.push_back(cConnection);
    for (const auto& [cConnection, cSave] : m_recipients)
        if (std::ranges::find(games, cConnection) == games.end())
            games.push_back(cConnection);
    NotifyPartyReload reload;
    reload.Save = acSave;
    for (const ConnectionId_t cGame : games)
    {
        m_network.Send(cGame, reload);
        m_recipients[cGame] = acSave;
        if (Holds(cGame, acSave))
        {
            spdlog::info("HostSaveTransfer: {:x} holds the save: nothing to send", cGame);
            m_receivers.erase(cGame);
        }
        else
            m_receivers[cGame] = {.Save = acSave};
    }
}

void HostSaveTransfer::Update(uint64_t aTick) noexcept
{
    // The waiting ones share one save, made now if the host's game can.
    std::optional<std::string> made;
    for (auto it = m_receivers.begin(); it != m_receivers.end();)
    {
        Receiver& receiver = it->second;
        const ConnectionId_t cConnection = it->first;
        if (!receiver.Since)
            receiver.Since = aTick;
        bool failed = false;
        if (receiver.Save.empty())
        {
            if (!made && m_game.SaveWorld)
                made = m_game.SaveWorld();
            if (made)
            {
                receiver.Save = *made;
                failed = made->empty();
            }
        }
        if (!failed && !receiver.Save.empty() && !receiver.Data && m_game.EncodeSave)
        {
            const auto cHeld = m_held.find(cConnection);
            receiver.Data = m_game.EncodeSave(receiver.Save, cHeld != m_held.end() ? cHeld->second : std::vector<std::string>{});
            // The host's game no longer has it.
            failed = receiver.Data && receiver.Data->Data.empty();
        }
        if (!failed && receiver.Save.empty() && !receiver.Ahead && ++receiver.Tries >= kWaitNoticeTries &&
            (!receiver.ToldAtTry || receiver.Tries - receiver.ToldAtTry >= kWaitNoticeEvery ||
             aTick - receiver.ToldAt >= kWaitNoticeSeconds * 1000))
        {
            spdlog::info("HostSaveTransfer: {:x} waits for the host's game to save ({} ms)", cConnection, aTick - receiver.Since);
            receiver.ToldAtTry = receiver.Tries;
            receiver.ToldAt = aTick;
            m_network.Send(cConnection, NotifySaveWaiting{});
        }
        if (failed || (!receiver.Data && aTick - receiver.Since > kSaveSeconds * 1000))
        {
            spdlog::warn("HostSaveTransfer: no save for {:x} (failed {}, waited {} ms){}", cConnection, failed, aTick - receiver.Since,
                         receiver.Ahead ? "" : ": refused");
            if (!receiver.Ahead)
                Refuse(cConnection);
            it = m_receivers.erase(it);
            continue;
        }
        if (receiver.Data)
        {
            // Keep bulk data from growing an unbounded ENet backlog on a slow line, and a save sent ahead from
            // holding the game's own messages up.
            const auto* pPeer = m_network.GetPeer(cConnection);
            if (pPeer && Net::ReadQuality(cConnection, *pPeer).QueuedReliable > (receiver.Ahead ? kAheadQueue : kQueue))
            {
                ++it;
                continue;
            }
            const auto& cData = receiver.Data->Data;
            const size_t cBudget = receiver.Ahead ? kAheadBytesPerUpdate : kBytesPerUpdate;
            for (size_t sent = 0; sent < cBudget && receiver.Next < cData.size();)
            {
                const auto cPiece = receiver.Data->Piece(receiver.Next, receiver.Ahead);
                sent += cPiece.Data.size();
                receiver.Next += cPiece.Data.size();
                m_network.Send(cConnection, cPiece);
            }
            if (receiver.Next >= cData.size())
            {
                if (!receiver.Ahead)
                    m_recipients[cConnection] = receiver.Save;
                spdlog::info("HostSaveTransfer: {:x} save queued{}: {} bytes for an image of {}, {}", cConnection,
                             receiver.Ahead ? " ahead" : "", cData.size(), receiver.Data->Size,
                             receiver.Data->Base.empty() ? "whole" : "against a save it holds");
                it = m_receivers.erase(it);
                continue;
            }
        }
        ++it;
    }
}
