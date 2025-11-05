#include <NetworkBridge.h>

#include <Packet.hpp>
#include <Messages/ClientMessageFactory.h>
#include <Events/PacketEvent.h>

NetworkBridge::NetworkBridge(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
    , m_isListening(false)
{
    BindMessageHandlers();
}

NetworkBridge::~NetworkBridge()
{
    StopListening();
}

bool NetworkBridge::StartListening(uint16_t aPort, uint8_t aMaxPeers) noexcept
{
    if (m_isListening)
    {
        spdlog::warn("[NetworkBridge] Already listening");
        return true;
    }

    // TiltedPhoques::Server::Host returns true on success
    if (!Host(aPort, 60)) // 60fps tick rate
    {
        spdlog::error("[NetworkBridge] Failed to start listening on port {}", aPort);
        return false;
    }

    m_isListening = true;
    spdlog::info("[NetworkBridge] Started listening on port {} for up to {} peers", aPort, aMaxPeers);
    return true;
}

void NetworkBridge::StopListening() noexcept
{
    if (!m_isListening)
        return;

    Close();
    m_isListening = false;
    m_peerUsernames.clear();
    spdlog::info("[NetworkBridge] Stopped listening");
}

void NetworkBridge::Update() noexcept
{
    if (m_isListening)
    {
        TiltedPhoques::Server::Update();
    }
}

void NetworkBridge::BroadcastToAllPeers(const ServerMessage& aMessage) const noexcept
{
    if (!m_isListening)
        return;

    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    aMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()),
                                     static_cast<uint32_t>(writer.Size()));

    // Send to all connected peers
    SendToAll(&packet);

    s_allocator.Reset();
}

void NetworkBridge::SendToPeer(ConnectionId_t aConnectionId, const ServerMessage& aMessage) const noexcept
{
    if (!m_isListening)
        return;

    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    aMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()),
                                     static_cast<uint32_t>(writer.Size()));

    // TiltedPhoques::Server::Send takes ConnectionId and packet
    Server::Send(aConnectionId, &packet);

    s_allocator.Reset();
}

size_t NetworkBridge::GetPeerCount() const noexcept
{
    return GetClientCount();
}

void NetworkBridge::OnUpdate()
{
    // Called every tick by TiltedPhoques::Server
    // Currently nothing to do here, but can be used for periodic tasks
}

void NetworkBridge::OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("[NetworkBridge] Couldn't parse packet from {:x}", aConnectionId);
        return;
    }

    // Call the appropriate message handler
    const auto opcode = pMessage->GetOpcode();
    if (opcode < 256 && m_messageHandlers[opcode])
    {
        m_messageHandlers[opcode](pMessage, aConnectionId);
    }
    else
    {
        spdlog::warn("[NetworkBridge] No handler for opcode {} from {:x}", opcode, aConnectionId);
    }
}

void NetworkBridge::OnConnection(ConnectionId_t aConnectionId)
{
    spdlog::info("[NetworkBridge] Peer connected: {:x}", aConnectionId);
    m_peerUsernames[aConnectionId] = TiltedPhoques::String("Peer_") + std::to_string(aConnectionId);

    // TODO: Trigger connection event via dispatcher if needed
}

void NetworkBridge::OnDisconnection(ConnectionId_t aConnectionId, EDisconnectReason aReason)
{
    spdlog::info("[NetworkBridge] Peer disconnected: {:x}, reason: {}", aConnectionId, (int)aReason);
    m_peerUsernames.erase(aConnectionId);

    // TODO: Trigger disconnection event via dispatcher if needed
}

void NetworkBridge::HandleClientMessage(UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId) noexcept
{
    // This is called by message handlers
    // The specific message handling happens in Server services via PacketEvent
}

void NetworkBridge::BindMessageHandlers() noexcept
{
    // Use ClientMessageFactory to register all message types
    // Same pattern as GameServer
    auto handlerGenerator = [this](auto& x) {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId) {
            const auto pRealMessage = CastUnique<T>(std::move(apMessage));

            // Trigger PacketEvent via dispatcher - Server services will listen
            // Note: PacketEvent expects a Player*, but we're in P2P mode so we'll need
            // to adapt this. For now, trigger with nullptr and services will use ConnectionId
            m_dispatcher.trigger(Server::PacketEvent<T>(pRealMessage.get(), nullptr));

            // TODO: Store ConnectionId somewhere so services can access it
            // Maybe trigger a different event type that includes ConnectionId?
        };

        return false;
    };

    ClientMessageFactory::Visit(handlerGenerator);

    spdlog::debug("[NetworkBridge] Registered message handlers");
}
