#include <NetworkClient.h>

#include <Packet.hpp>
#include <Messages/ServerMessageFactory.h>

NetworkClient::NetworkClient(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
    , m_isConnected(false)
{
    BindMessageHandlers();
}

NetworkClient::~NetworkClient()
{
    Disconnect();
}

bool NetworkClient::ConnectToHost(const TiltedPhoques::String& aHostAddress, uint16_t aPort) noexcept
{
    if (m_isConnected)
    {
        spdlog::warn("[NetworkClient] Already connected to a host");
        return true;
    }

    // TiltedPhoques::Client::Connect returns true on success
    if (!Connect(aHostAddress, aPort))
    {
        spdlog::error("[NetworkClient] Failed to connect to host at {}:{}", aHostAddress, aPort);
        return false;
    }

    spdlog::info("[NetworkClient] Initiated connection to host at {}:{}", aHostAddress, aPort);
    return true;
}

void NetworkClient::Disconnect() noexcept
{
    if (!m_isConnected)
        return;

    Close();
    m_isConnected = false;
    spdlog::info("[NetworkClient] Disconnected from host");
}

void NetworkClient::Update() noexcept
{
    if (m_isConnected)
    {
        TiltedPhoques::Client::Update();
    }
}

void NetworkClient::SendToHost(const ClientMessage& aMessage) noexcept
{
    if (!m_isConnected)
    {
        spdlog::warn("[NetworkClient] Cannot send message, not connected to host");
        return;
    }

    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    TiltedPhoques::ScopedAllocator _{s_allocator};

    Buffer buffer(1 << 16);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    aMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()),
                                     static_cast<uint32_t>(writer.Size()));

    Client::Send(&packet);
}

void NetworkClient::OnUpdate()
{
    // Called every tick by TiltedPhoques::Client
    // Currently nothing to do here, but can be used for periodic tasks
}

void NetworkClient::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("[NetworkClient] Couldn't parse packet from host");
        return;
    }

    // Call the appropriate message handler
    const auto opcode = pMessage->GetOpcode();
    if (opcode < 256 && m_messageHandlers[opcode])
    {
        m_messageHandlers[opcode](pMessage);
    }
    else
    {
        spdlog::warn("[NetworkClient] No handler for opcode {} from host", opcode);
    }
}

void NetworkClient::OnConnected()
{
    m_isConnected = true;
    spdlog::info("[NetworkClient] Connected to host");

    // TODO: Trigger connection event via dispatcher if needed
    // TODO: Send authentication request?
}

void NetworkClient::OnDisconnected(uint64_t aReasonCode)
{
    m_isConnected = false;
    spdlog::info("[NetworkClient] Disconnected from host, reason code: {}", aReasonCode);

    // TODO: Trigger disconnection event via dispatcher if needed
}

void NetworkClient::OnAuthenticated()
{
    spdlog::info("[NetworkClient] Authenticated with host");

    // TODO: Trigger authenticated event via dispatcher if needed
}

void NetworkClient::HandleServerMessage(UniquePtr<ServerMessage>& apMessage) noexcept
{
    // This is called by message handlers
    // The specific message handling happens in client services via dispatcher events
}

void NetworkClient::BindMessageHandlers() noexcept
{
    // Use ServerMessageFactory to register all message types
    // Same pattern as TransportService
    auto handlerGenerator = [this](auto& x) {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage) {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));

            // Trigger the message directly via dispatcher - client services will listen
            // Same pattern as TransportService
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    spdlog::debug("[NetworkClient] Registered message handlers");
}
