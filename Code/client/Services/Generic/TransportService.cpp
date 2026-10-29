
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Services/Host/JoinRefusal.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <World.h>
#include <Diagnostics/MessageTrail.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IWorldBridge.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/NotifyMessageFactory.h>
#include <Services/Host/HostNetwork.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/Relayed.h>
#include <network/Packet.h>

#include <RunningGame.h>
#include <Services/JoinService.h>
#include <Events/SessionLeavingEvent.h>
#include <Structs/GameVersions.h>
#include <BuildVersion.h>
#include <FormIds.h>

// #include <imgui_internal.h>

using Net::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_playerBridge(aWorld.ctx().at<IPlayerBridge&>())
    , m_formBridge(aWorld.ctx().at<IFormBridge&>())
    , m_worldBridge(aWorld.ctx().at<IWorldBridge&>())
{
    Listen<UpdateEvent, &TransportService::HandleUpdate>(m_connections, m_dispatcher, this);
    Listen<ConnectedEvent, &TransportService::HandleConnected>(m_connections, m_dispatcher, this);
    Listen<DisconnectedEvent, &TransportService::HandleDisconnected>(m_connections, m_dispatcher, this);

    m_connected = false;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](std::unique_ptr<NotifyMessage>& apMessage)
        {
            const auto pRealMessage = CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    NotifyMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](std::unique_ptr<NotifyMessage>& apMessage)
    {
        const auto pRealMessage = CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
    m_messageHandlers[NotifyRecipes::Opcode] = [this](std::unique_ptr<NotifyMessage>& apMessage) { HandleRecipes(*apMessage); };
}

// The host's game's in-process connection to its own network. Real connection ids are enet peer
// addresses, so this one cannot collide.
static constexpr Net::ConnectionId_t kLocalConnectionId = 1;

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
    m_endpoint = acEndpoint;
    return Client::Connect(acEndpoint);
}

bool TransportService::IsConnected() const noexcept
{
    return m_pHostNetwork != nullptr || Client::IsConnected();
}

void TransportService::Close(DisconnectedEvent::Cause aWhy) noexcept
{
    if (IsOnline() && aWhy == DisconnectedEvent::Cause::kLeft)
        m_dispatcher.trigger(SessionLeavingEvent{});
    m_closing = aWhy;
    if (!m_pHostNetwork)
    {
        Client::Close();
        m_closing.reset();
        return;
    }

    auto* pNetwork = m_pHostNetwork;
    m_pHostNetwork = nullptr;
    m_localInbox.clear();
    pNetwork->CloseRoute(kLocalConnectionId);
    m_closingRoute = true;
    OnDisconnected(kNormal);
    m_closingRoute = false;
    m_closing.reset();
}

std::string TransportService::GetLocalPlayerName() const noexcept
{
    const std::string cName = m_formBridge.GetFormName(m_playerBridge.GetPlayerBaseNpcFormId());
    return cName.empty() ? "Some dragon boi" : cName;
}

uint64_t TransportService::GetTick() const noexcept
{
    return m_pHostNetwork ? m_pHostNetwork->GetTick() : GetClock().GetCurrentTick();
}

bool TransportService::Send(const RequestMessage& acMessage) const noexcept
{
    if (m_pHostNetwork)
    {
        // The host's rules handle it right away.
        Wire::Buffer buffer(1 << 16);
        Wire::Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        Diagnostics::RecordMessage(GetTick(), Diagnostics::MessageWay::Sent, RequestName(acMessage.GetOpcode()), 0, static_cast<uint32_t>(writer.Size()));
        m_pHostNetwork->Receive(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()), kLocalConnectionId);
        return true;
    }

    if (IsConnected())
    {
        SendRecipes();
        SendPacket(acMessage);
        return true;
    }

    return false;
}

void TransportService::SendPacket(const RequestMessage& acMessage) const noexcept
{
    Wire::Buffer buffer(1 << 16);
    Wire::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Write first byte as packet needs it

    acMessage.Serialize(writer);

    Net::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
    Diagnostics::RecordMessage(GetTick(), Diagnostics::MessageWay::Sent, RequestName(acMessage.GetOpcode()), 0, static_cast<uint32_t>(writer.Size()));

    Client::Send(&packet, acMessage.GetOpcode() == kPartyTravelRequest ? Net::kReliableNoNagle : Net::kReliable);
}

void TransportService::SendRecipes() const noexcept
{
    // Only once in the session: the host takes nothing before the authentication.
    if (!IsOnline())
        return;
    RecipesRequest request;
    request.Known = m_world.GetRecipes().Unsent(m_recipesSent);
    if (!request.Known.empty())
        SendPacket(request);
}

void TransportService::HandleRecipes(const NotifyMessage& acMessage) noexcept
{
    // The host gets them back with this game's own (it keeps each once).
    for (const Recipe& cRecipe : static_cast<const NotifyRecipes&>(acMessage).Known)
        m_world.GetRecipes().Learn(cRecipe);
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    NotifyMessageFactory factory;
    Wire::Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from the host");
        return;
    }

    Diagnostics::RecordMessage(GetTick(), Diagnostics::MessageWay::Received, NotifyName(pMessage->GetOpcode()), 0, aSize);
    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}

void TransportService::OnConnected()
{
    m_recipesSent = 0;
    AuthenticationRequest request{};
    request.Version = BuildCommit();
    request.Game = RunningGame::GetVersions();

    // null if discord is not active
    // TODO: think about user opt out

    request.Username = GetLocalPlayerName();

    request.UserMods = RunningGame::Plugins(m_formBridge);

    const uint32_t worldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();
    if (worldSpaceId != 0)
        request.WorldSpaceId = ToSession(worldSpaceId).value_or(GameId{});

    request.CellId = ToSession(m_playerBridge.GetPlayerParentCellId()).value_or(GameId{});

    request.Level = m_playerBridge.GetPlayerLevel();

    request.PlayerTime.TimeScale = m_worldBridge.GetGameTimeScale();
    request.PlayerTime.Time = m_worldBridge.GetGameTimeHours();
    request.PlayerTime.Year = m_worldBridge.GetGameYear();
    request.PlayerTime.Month = m_worldBridge.GetGameMonth();
    request.PlayerTime.Day = m_worldBridge.GetGameDay();

    if (const auto* pJoin = m_world.ctx().find<JoinService>())
    {
        request.PlayerKey = pJoin->PlayerKey();
        request.WantsWorld = pJoin->WantsWorld();
        request.HasCharacter = pJoin->HasCharacter();
    }

    Send(request);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    using Cause = DisconnectedEvent::Cause;
    DisconnectedEvent event;
    event.WasGuest = m_connected && !m_closingRoute;
    event.Why = m_closing ? *m_closing : aReason == kHostLeft ? Cause::kHostLeft : aReason == kKicked ? Cause::kKicked : aReason == kAborted ? Cause::kLeft : Cause::kLost;
    m_connected = false;
    // A refusal stays what the attempt ended with; why a session ended comes with leaving and kicks.
    if (m_joinStatus.Current != JoinStatus::State::kRefused)
        m_joinStatus = {};

    spdlog::warn("Disconnected from the host ({})", std::to_underlying(aReason));

    m_dispatcher.trigger(event);
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

        NotifySettingsChange difficulty;
        difficulty.Difficulty = acMessage.Difficulty;
        m_dispatcher.trigger(difficulty);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId, acMessage.CharacterId));
        return; // quit the function here.
    }

    // Joining from the main menu: the gate passed, the host's save follows (JoinService).
    if (acMessage.Type == AR::kWorldFollows)
    {
        m_dispatcher.trigger(acMessage);
        return;
    }

    // Refused: the join status says why.
    m_joinStatus = JoinRefusal::ForPlayer(acMessage, {BuildCommit(), RunningGame::GetVersions(), RunningGame::Plugins(m_formBridge)});
    if (acMessage.Type == AR::kNoCharacter)
        spdlog::info("The host has no kept character; JoinService will open character creation if available");
    else
        spdlog::error("Couldn't join the session ({}): {}", m_joinStatus.Reason, m_joinStatus.Message);
    m_dispatcher.trigger(acMessage);
}
