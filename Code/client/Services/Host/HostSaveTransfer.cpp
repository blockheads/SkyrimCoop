#include <Services/Host/HostSaveTransfer.h>

#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>

#include <Messages/AuthenticationResponse.h>

HostSaveTransfer::HostSaveTransfer(HostNetwork& aNetwork, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_game(acGame)
{
}

void HostSaveTransfer::Want(ConnectionId_t aConnection) noexcept
{
    m_receivers[aConnection] = {};
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
            if (!receiver.Since)
                receiver.Since = aTick;
            if (!pSave && m_game.SaveWorld)
                if (auto save = m_game.SaveWorld())
                {
                    pSave = std::make_shared<const Chunks>(std::move(*save));
                    spdlog::info("HostSaveTransfer: the host's save made, {} chunk(s)", pSave->size());
                }
            if (pSave)
                receiver.Save = pSave;
            else if (aTick - receiver.Since > kSaveSeconds * 1000)
            {
                spdlog::warn("HostSaveTransfer: no save for {:x} in {} s: refused", cConnection, kSaveSeconds);
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
            for (size_t i = 0; i < kChunksPerUpdate && receiver.Next < receiver.Save->size(); ++i)
                m_network.Send(cConnection, (*receiver.Save)[receiver.Next++]);
            if (receiver.Next == receiver.Save->size())
            {
                spdlog::info("HostSaveTransfer: {:x} has the host's save", cConnection);
                it = m_receivers.erase(it);
                continue;
            }
        }
        ++it;
    }
}
