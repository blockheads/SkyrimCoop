
#include <Services/TransportService.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Bridges/IPlayerBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IWorldBridge.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <GameServer.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>

#include <ScriptExtender.h>
#include <Services/DiscordService.h>

// #include <imgui_internal.h>

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

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

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
}

// The host's in-process connection to its embedded server. Real connection ids are enet peer
// addresses, so this one cannot collide.
static constexpr ConnectionId_t kLocalConnectionId = 1;

void TransportService::ConnectLocal(Server::GameServer& aServer) noexcept
{
    if (m_pLocalServer || Client::IsConnected())
        return;

    m_pLocalServer = &aServer;
    m_localInbox.clear();
    // The server's replies arrive while it handles a message: queued, so none of our handlers runs
    // inside another service's.
    aServer.OpenLoopback(kLocalConnectionId, [this](const void* apData, uint32_t aSize) {
        const auto* pData = static_cast<const uint8_t*>(apData);
        m_localInbox.emplace_back(pData, pData + aSize);
    });
    spdlog::info("Connected to the embedded server in this process");
    OnConnected();
}

bool TransportService::IsConnected() const noexcept
{
    return m_pLocalServer != nullptr || Client::IsConnected();
}

void TransportService::Close() noexcept
{
    if (!m_pLocalServer)
    {
        Client::Close();
        return;
    }

    auto* pServer = m_pLocalServer;
    m_pLocalServer = nullptr;
    m_localInbox.clear();
    pServer->CloseLoopback();
    OnDisconnected(kNormal);
}

uint64_t TransportService::GetTick() const noexcept
{
    return m_pLocalServer ? m_pLocalServer->GetTick() : GetClock().GetCurrentTick();
}

bool TransportService::Send(const ClientMessage& acMessage) const noexcept
{
    static thread_local ScratchAllocator s_allocator(1 << 18);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (m_pLocalServer)
    {
        // The server handles it right away, and must not allocate from our scratch allocator: the
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
        m_pLocalServer->InjectMessage(payload.data(), static_cast<uint32_t>(payload.size()), kLocalConnectionId);
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
    ServerMessageFactory factory;
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
    request.SKSEActive = IsScriptExtenderLoaded();
    request.MO2Active = GetModuleHandleW(kMO2DllName);

    request.Token = m_serverPassword;
    m_serverPassword = "";

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;

    // Player identity — use bridge instead of direct SKSE calls
    const std::string formName = m_formBridge.GetFormName(m_playerBridge.GetPlayerBaseNpcFormId());
    request.Username = formName.empty() ? "Some dragon boi" : formName.c_str();

    // Mod list
    for (const auto& modEntry : m_formBridge.GetLoadedMods())
    {
        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Id = modEntry.Id;
        entry.IsLite = modEntry.IsLite;
        entry.Filename = modEntry.Filename.c_str();
    }

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

    spdlog::warn("Disconnected from server {}", aReason);

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pLocalServer)
    {
        Update();
        return;
    }

    // Handling one can queue more (or close the connection): take the batch first.
    const auto cInbox = std::move(m_localInbox);
    m_localInbox.clear();
    for (const auto& cPacket : cInbox)
    {
        if (!m_pLocalServer)
            break;
        // The server's packet type byte first: the loopback only carries plain payloads.
        if (cPacket.size() > 1)
            OnConsume(cPacket.data() + 1, static_cast<uint32_t>(cPacket.size() - 1));
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

        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        return; // quit the function here.
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BuildCommit());
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
        ErrorInfo += "\"MO2\"";
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    m_dispatcher.trigger(errorEvent);
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
