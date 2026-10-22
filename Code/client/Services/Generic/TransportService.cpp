
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Services/Host/JoinRefusal.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IWorldBridge.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/NotifyMessageFactory.h>
#include <Services/Host/HostNetwork.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>

#include <RunningGame.h>
#include <Structs/GameVersions.h>

// #include <imgui_internal.h>

using TiltedPhoques::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_playerBridge(aWorld.ctx().at<IPlayerBridge&>())
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
    , m_worldBridge(aWorld.ctx().at<IWorldBridge&>())
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_connected = false;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<NotifyMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    NotifyMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<NotifyMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
}

// The host's game's in-process connection to its own network. Real connection ids are enet peer
// addresses, so this one cannot collide.
static constexpr TiltedPhoques::ConnectionId_t kLocalConnectionId = 1;

void TransportService::ConnectLocal(HostNetwork& aNetwork) noexcept
{
    if (m_pHostNetwork || Client::IsConnected())
        return;

    m_pHostNetwork = &aNetwork;
    m_localInbox.clear();
    m_joinStatus = {JoinStatus::State::kConnecting};
    // Replies arrive while the host's rules handle a message: queued, so none of our handlers runs
    // inside another service's.
    aNetwork.OpenRoute(kLocalConnectionId, [this](const void* apData, uint32_t aSize) {
        const auto* pData = static_cast<const uint8_t*>(apData);
        m_localInbox.emplace_back(pData, pData + aSize);
    });
    spdlog::info("Connected to the host's network in this process");
    OnConnected();
}

bool TransportService::Connect(const std::string& acEndpoint) noexcept
{
    m_joinStatus = {JoinStatus::State::kConnecting};
    return Client::Connect(acEndpoint);
}

bool TransportService::IsConnected() const noexcept
{
    return m_pHostNetwork != nullptr || Client::IsConnected();
}

void TransportService::Close() noexcept
{
    if (!m_pHostNetwork)
    {
        Client::Close();
        return;
    }

    auto* pNetwork = m_pHostNetwork;
    m_pHostNetwork = nullptr;
    m_localInbox.clear();
    pNetwork->CloseRoute(kLocalConnectionId);
    OnDisconnected(kNormal);
}

uint64_t TransportService::GetTick() const noexcept
{
    return m_pHostNetwork ? m_pHostNetwork->GetTick() : GetClock().GetCurrentTick();
}

bool TransportService::Send(const RequestMessage& acMessage) const noexcept
{
    static thread_local ScratchAllocator s_allocator(1 << 18);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (m_pHostNetwork)
    {
        // The host's rules handle it right away, and must not allocate from our scratch allocator: the
        // bytes are copied out (into room reserved beforehand) and handed over after its scope.
        Vector<uint8_t> payload;
        payload.reserve(1 << 16);
        {
            ScopedAllocator _{s_allocator};
            Buffer buffer(1 << 16);
            Buffer::Writer writer(&buffer);
            acMessage.Serialize(writer);
            payload.assign(buffer.GetWriteData(), buffer.GetWriteData() + writer.Size());
        }
        m_pHostNetwork->Receive(payload.data(), static_cast<uint32_t>(payload.size()), kLocalConnectionId);
        return true;
    }

    if (IsConnected())
    {
        ScopedAllocator _{s_allocator};

        Buffer buffer(1 << 16);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // Write first byte as packet needs it

        acMessage.Serialize(writer);

        TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

        Client::Send(&packet);

        return true;
    }

    return false;
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    NotifyMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}

void TransportService::OnConnected()
{
    AuthenticationRequest request{};
    request.Version = BuildCommit();
    request.Game = RunningGame::GetVersions();

    request.Token = m_serverPassword;
    m_serverPassword = "";

    // null if discord is not active
    // TODO: think about user opt out

    // Player identity — use bridge instead of direct SKSE calls
    const std::string formName = m_formBridge.GetFormName(m_playerBridge.GetPlayerBaseNpcFormId());
    request.Username = formName.empty() ? "Some dragon boi" : formName.c_str();

    request.UserMods = RunningGame::Plugins(m_formBridge);

    auto& modSystem = m_world.GetModSystem();

    const uint32_t worldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();
    if (worldSpaceId != 0)
        modSystem.GetServerModId(worldSpaceId, request.WorldSpaceId);

    modSystem.GetServerModId(m_playerBridge.GetPlayerParentCellId(), request.CellId);

    request.Level = m_playerBridge.GetPlayerLevel();

    request.PlayerTime.TimeScale = m_worldBridge.GetGameTimeScale();
    request.PlayerTime.Time = m_worldBridge.GetGameTimeHours();
    request.PlayerTime.Year = m_worldBridge.GetGameYear();
    request.PlayerTime.Month = m_worldBridge.GetGameMonth();
    request.PlayerTime.Day = m_worldBridge.GetGameDay();

    Send(request);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    m_connected = false;
    // A refusal stays what the attempt ended with; why a session ended comes with leaving and kicks.
    if (m_joinStatus.Current != JoinStatus::State::kRefused)
        m_joinStatus = {};

    spdlog::warn("Disconnected from server {}", aReason);

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pHostNetwork)
    {
        Update();
        return;
    }

    // Handling one can queue more (or close the connection): take the batch first.
    const auto cInbox = std::move(m_localInbox);
    m_localInbox.clear();
    for (const auto& cPacket : cInbox)
    {
        if (!m_pHostNetwork)
            break;
        if (!cPacket.empty())
            OnConsume(cPacket.data(), static_cast<uint32_t>(cPacket.size()));
    }
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        m_connected = true;
        m_joinStatus = {JoinStatus::State::kJoined};

        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        return; // quit the function here.
    }

    // Refused: the join status says why.
    m_joinStatus = JoinRefusal::ForPlayer(acMessage, {BuildCommit(), RunningGame::GetVersions(), RunningGame::Plugins(m_formBridge)});
    spdlog::error("Couldn't join the session ({}): {}", m_joinStatus.Reason, m_joinStatus.Message);
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
