#include <Services/Host/HostSaveTransfer.h>

#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/DeathMessages.h>

HostSaveTransfer::HostSaveTransfer(HostNetwork& aNetwork, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_game(acGame)
{
}

void HostSaveTransfer::Want(ConnectionId_t aConnection, const std::vector<std::string>& acCachedBlocks) noexcept
{
    auto& receiver = m_receivers[aConnection] = {};
    if (acCachedBlocks.size() <= 16384)
        for (const auto& hash : acCachedBlocks)
            if (hash.size() == 32)
                receiver.Cached.insert(hash);
}

void HostSaveTransfer::OnDisconnected(ConnectionId_t aConnection) noexcept
{
    m_receivers.erase(aConnection);
}

void HostSaveTransfer::Update(uint64_t aTick) noexcept
{
    // The waiting ones share one save, made now if the host's game can.
    std::shared_ptr<const Chunks> pSave;
    for (auto it = m_receivers.begin(); it != m_receivers.end();)
    {
        Receiver& receiver = it->second;
        const ConnectionId_t cConnection = it->first;
        if (!receiver.Save)
        {
            bool invalidSave = false;
            if (!receiver.Since)
                receiver.Since = aTick;
            if (!pSave && m_game.SaveWorld)
                if (auto save = m_game.SaveWorld())
                {
                    if (save->empty())
                        invalidSave = true;
                    else
                    {
                        pSave = std::make_shared<const Chunks>(std::move(*save));
                        spdlog::info("HostSaveTransfer: the host's save made, {} chunk(s)", pSave->size());
                    }
                }
            if (pSave)
                receiver.Save = pSave;
            else if (invalidSave || aTick - receiver.Since > kSaveSeconds * 1000)
            {
                spdlog::warn("HostSaveTransfer: no transferable save for {:x} (invalid {}, waited {} ms): refused",
                             cConnection, invalidSave, aTick - receiver.Since);
                AuthenticationResponse response;
                response.Type = AuthenticationResponse::ResponseType::kTransferFailed;
                m_network.Send(cConnection, response);
                m_network.Kick(cConnection);
                it = m_receivers.erase(it);
                continue;
            }
        }
        if (receiver.Save)
        {
            // Keep bulk data from growing an unbounded ENet backlog on a slow line.
            if (const auto* pPeer = m_network.GetPeer(cConnection); pPeer && Net::ReadQuality(cConnection, *pPeer).QueuedReliable > 512)
            {
                ++it;
                continue;
            }
            size_t bytes = 0;
            for (size_t i = 0; i < kChunksPerUpdate && receiver.Next < receiver.Save->size(); ++i)
            {
                const auto& chunk = (*receiver.Save)[receiver.Next];
                const size_t cCost = (receiver.Cached.contains(chunk.Hash) ? 0 : chunk.Data.size()) + 64;
                if (bytes && bytes + cCost > kBytesPerUpdate)
                    break;
                bytes += cCost;
                ++receiver.Next;
                if (receiver.Cached.contains(chunk.Hash))
                {
                    auto reference = chunk;
                    receiver.ReusedBytes += chunk.RawSize;
                    reference.Data.clear();
                    m_network.Send(cConnection, reference);
                }
                else
                {
                    receiver.SentBytes += chunk.Data.size();
                    m_network.Send(cConnection, chunk);
                }
            }
            if (receiver.Next == receiver.Save->size())
            {
                spdlog::info("HostSaveTransfer: {:x} save queued: {} payload bytes sent, {} raw bytes reused",
                             cConnection, receiver.SentBytes, receiver.ReusedBytes);
                it = m_receivers.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void HostSaveTransfer::Checkpoint(std::vector<NotifySaveChunk> aChunks, const std::vector<ConnectionId_t>& acGuests) noexcept
{
    for (const ConnectionId_t cGuest : acGuests)
        m_receivers.try_emplace(cGuest);
    const auto cpSave = std::make_shared<const Chunks>(std::move(aChunks));
    for (auto& [cConnection, receiver] : m_receivers)
    {
        m_network.Send(cConnection, NotifyPartyReload{});
        receiver = {cpSave};
    }
}
