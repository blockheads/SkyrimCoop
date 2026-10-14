#include <Components.h>
#include <GameServer.h>
#include <Packet.hpp>
#include <Recording/EnetRecorder.h>

#include <Events/AdminPacketEvent.h>
#include <Events/PacketEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <AdminMessages/AdminSessionOpen.h>
#include <AdminMessages/ClientAdminMessageFactory.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <console/ConsoleRegistry.h>
#include <resources/ResourceCollection.h>

// -- Cvars --
ServerConsole::Setting uServerPort{"GameServer:uPort", "Which port to host the server on", 10578u};
ServerConsole::Setting bPremiumTickrate{"GameServer:bPremiumMode", "Use premium tick rate", true};

// Dedicated server settings removed (P2P host-only, per Phase 5 D-01)

// -- Commands --
ServerConsole::Command<> TogglePremium("TogglePremium", "Toggle Premium Tickrate on/off", [](ServerConsole::ArgStack&) {
    bPremiumTickrate = !bPremiumTickrate;
    spdlog::get("ConOut")->info("Premium Tickrate has been {}.", bPremiumTickrate == true ? "enabled" : "disabled");
});

ServerConsole::Command<> ShowVersion("version", "Show the version the server was compiled with",
                                     [](ServerConsole::ArgStack&) {
                                         spdlog::get("ConOut")->info("Server {}", BuildCommit());
                                     });

ServerConsole::Command<> CrashServer("crash", "Crashes the server, don't use!", [](ServerConsole::ArgStack&) {
    int* i = 0;
    *i = 42;
});

static uint16_t GetUserTickRate()
{
    return bPremiumTickrate ? 60 : 30;
}

namespace Server
{

GameServer::GameServer(ServerConsole::ConsoleRegistry& aConsole) noexcept
    : m_lastFrameTime(std::chrono::high_resolution_clock::now()),
      m_startTime(std::chrono::high_resolution_clock::now()), m_commands(aConsole), m_requestStop(false)
{
    BASE_ASSERT(s_pInstance == nullptr, "Server instance already exists?");
    s_pInstance = this;

    auto port = uServerPort.value_as<uint16_t>();
    while (!Host(port, GetUserTickRate()))
    {
        spdlog::warn("Port {} is already in use, trying {}", port, port + 1);
        port++;
    }

    // P2P host-only: no password, simplified info
    m_info.name = "SkyrimCoop Host";
    m_info.desc = "";
    m_info.icon_url = "";
    m_info.tagList = "";
    m_info.tick_rate = GetUserTickRate();

    spdlog::info("Server {} started on port {}", BuildCommit(), GetPort());
    UpdateTitle();

    m_pWorld = MakeUnique<World>();

    BindMessageHandlers();

    m_pResources = MakeUnique<Resources::ResourceCollection>();
}

GameServer::GameServer(ServerConsole::ConsoleRegistry& aConsole, GameServerTestMode) noexcept
    : m_lastFrameTime(std::chrono::high_resolution_clock::now()),
      m_startTime(std::chrono::high_resolution_clock::now()), m_commands(aConsole), m_requestStop(false)
{
    // Test mode: no port binding, no ESLoader, no ScriptService, no resources
    s_pInstance = this;

    m_pWorld = MakeUnique<World>(WorldTestMode{});
    BindMessageHandlers();

    spdlog::info("GameServer: test mode initialized (no enet6 port, no ESLoader)");
}

GameServer::~GameServer()
{
    s_pInstance = nullptr;
}

GameServer* GameServer::Get() noexcept
{
    return s_pInstance;
}

void GameServer::StartRecording(const std::string& aOutputDir)
{
    m_pRecorder = std::make_unique<EnetRecorder>(aOutputDir);
}

void GameServer::StopRecording()
{
    m_pRecorder.reset();
}

void GameServer::Initialize()
{
    BindServerCommands();
    m_pWorld->GetScriptService().Initialize(*m_pResources);
}

void GameServer::Kill()
{
    spdlog::info("Server shutdown requested");
    m_requestStop = true;
}

void GameServer::BindMessageHandlers()
{
    auto handlerGenerator = [this](auto& x) {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId) {
            auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

            if (!pPlayer)
            {
                spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
                Kick(aConnectionId);
                return;
            }

            const auto pRealMessage = CastUnique<T>(std::move(apMessage));

            m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));
        };

        return false;
    };

    ClientMessageFactory::Visit(handlerGenerator);

    auto adminHandlerGenerator = [this](auto& x) {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_adminMessageHandlers[T::Opcode] = [this](UniquePtr<ClientAdminMessage>& apMessage,
                                                   ConnectionId_t aConnectionId) {
            const auto pRealMessage = CastUnique<T>(std::move(apMessage));
            m_pWorld->GetDispatcher().trigger(AdminPacketEvent<T>(pRealMessage.get(), aConnectionId));
        };

        return false;
    };

    ClientAdminMessageFactory::Visit(adminHandlerGenerator);
}

void GameServer::BindServerCommands()
{
    m_commands.RegisterCommand<>("uptime", "Show how long the server has been running for",
                                 [this](ServerConsole::ArgStack&) {
                                     Uptime uptime = GetUptime();
                                     spdlog::get("ConOut")->info("Server uptime: {}w {}d {}h {}m", uptime.weeks,
                                                                 uptime.days, uptime.hours, uptime.minutes);
                                 });

    m_commands.RegisterCommand<>("players", "List all players on this server", [&](ServerConsole::ArgStack&) {
        auto out = spdlog::get("ConOut");
        uint32_t count = m_pWorld->GetPlayerManager().Count();
        if (count == 0)
        {
            out->warn("No players on here. Invite some friends!");
            return;
        }

        out->info("<------Players-({})--->", count);
        for (Player* pPlayer : m_pWorld->GetPlayerManager())
        {
            out->info("{}: {}", pPlayer->GetId(), pPlayer->GetUsername().c_str());
        }
    });

    m_commands.RegisterCommand<>("mods", "List all installed mods on this server", [&](ServerConsole::ArgStack&) {
        auto out = spdlog::get("ConOut");
        auto& mods = m_pWorld->ctx().at<ModsComponent>().GetServerMods();
        if (mods.size() == 0)
        {
            out->warn("No mods installed");
            return;
        }

        out->info("<------Mods-({})--->", mods.size());
        for (auto& it : mods)
        {
            out->info(it.first);
        }
    });

    m_commands.RegisterCommand<>("resources", "List all loaded resources on the server", [&](ServerConsole::ArgStack&) {
        auto out = spdlog::get("ConOut");
        if (!m_pResources || m_pResources->GetManifests().size() == 0)
        {
            out->warn("No resources loaded");
            return;
        }

        out->info("<------Resources-({})--->", m_pResources->GetManifests().size());
        m_pResources->ForEachManifest([&](const auto& aManifest) {
            out->info("{} -> {}", aManifest.Name.c_str(), aManifest.Description.c_str());
        });
    });

    m_commands.RegisterCommand<>("quit", "Stop the server", [&](ServerConsole::ArgStack&) { Kill(); });

    m_commands.RegisterCommand<std::string>("AddAdmin", "Add admin privileges to player",
                                            [&](ServerConsole::ArgStack& aStack) {
                                                auto out = spdlog::get("ConOut");

                                                const auto& cUsername = aStack.Pop<String>();
                                                if (GetAdminByUsername(cUsername))
                                                {
                                                    out->info("{} is already an admin", cUsername.c_str());
                                                    return;
                                                }

                                                auto* pPlayer = PlayerManager::Get()->GetByUsername(cUsername);
                                                if (pPlayer)
                                                {
                                                    AddAdminSession(pPlayer->GetConnectionId());
                                                    out->info("{} admin privileges added", cUsername.c_str());
                                                }
                                                else
                                                {
                                                    // retry after sanitizing username
                                                    String backupUsername = SanitizeUsername(cUsername);
                                                    pPlayer = PlayerManager::Get()->GetByUsername(backupUsername);

                                                    if (pPlayer)
                                                    {
                                                        AddAdminSession(pPlayer->GetConnectionId());
                                                        out->info("{} admin privileges added", cUsername.c_str());
                                                    }
                                                    else
                                                    {
                                                        out->warn("{} is not a valid player", backupUsername.c_str());
                                                    }
                                                }
                                            });
    m_commands.RegisterCommand<std::string>("RemoveAdmin", "Remove admin privileges from player",
                                            [&](ServerConsole::ArgStack& aStack) {
                                                auto out = spdlog::get("ConOut");

                                                const auto& cUsername = aStack.Pop<String>();
                                                auto* pPlayer = GetAdminByUsername(cUsername);

                                                if (pPlayer)
                                                {
                                                    RemoveAdminSession(pPlayer->GetConnectionId());
                                                    out->info("{} admin privileges revoked", cUsername.c_str());
                                                }
                                                else
                                                {
                                                    // retry after sanitizing username
                                                    String backupUsername = SanitizeUsername(cUsername);
                                                    pPlayer = GetAdminByUsername(backupUsername);

                                                    if (pPlayer)
                                                    {
                                                        RemoveAdminSession(pPlayer->GetConnectionId());
                                                        out->info("{} admin privileges revoked", cUsername.c_str());
                                                    }
                                                    else
                                                    {
                                                        out->warn("{} is not an admin", backupUsername.c_str());
                                                    }
                                                }
                                            });
    m_commands.RegisterCommand<>("admins", "List all admins", [&](ServerConsole::ArgStack&) {
        auto out = spdlog::get("ConOut");
        if (m_adminSessions.size() == 0)
        {
            out->warn("No admins");
            return;
        }

        String output = "Admins: ";
        bool _first = true;

        for (const auto& cAdminSession : m_adminSessions)
        {
            auto* pPlayer = PlayerManager::Get()->GetByConnectionId(cAdminSession);

            if (!pPlayer)
            {
                out->error("Admin session not found: {}", cAdminSession);
                continue;
            }

            const auto& cUsername = pPlayer->GetUsername();

            if (_first)
            {
                _first = false;
            }
            else
            {
                output += ", ";
            }
            output += cUsername;
        }

        out->info("{}", output.c_str());
    });
}


void GameServer::OnUpdate()
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(cDelta).count();

    auto& dispatcher = m_pWorld->GetDispatcher();

    dispatcher.trigger(UpdateEvent{cDeltaSeconds});


    if (m_requestStop)
        Close();
}

void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    // TODO: ClientAdminMessageFactory
    /*if (m_adminSessions.contains(aConnectionId)) [[unlikely]]
    {
        const ClientAdminMessageFactory factory;
        auto pMessage = factory.Extract(reader);
        if (!pMessage)
        {
            spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
            return;
        }

        m_adminMessageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
    }
    else
    {*/
    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
        return;
    }

    // Recording tap: capture incoming ClientMessage as NDJSON
    if (m_pRecorder)
        m_pRecorder->RecordClientMessage(aConnectionId, *pMessage);

    if (m_frontDoor && m_frontDoor(*pMessage, aConnectionId))
        return;

    spdlog::info("GameServer::OnConsume opcode={} from {:x} (size={})", pMessage->GetOpcode(), aConnectionId, aSize);
    m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
    //}
}

void GameServer::OnConnection(const ConnectionId_t aHandle)
{
    spdlog::info("Connection received {:x}", aHandle);
    UpdateTitle();
}

void GameServer::OnDisconnection(const ConnectionId_t aConnectionId, EDisconnectReason aReason)
{
    m_adminSessions.erase(aConnectionId);

    auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

    spdlog::info("Connection ended {:x} - '{}' disconnected", aConnectionId,
                 (pPlayer != NULL ? pPlayer->GetUsername().c_str() : "NULL"));

    m_pWorld->GetScriptService().HandlePlayerQuit(aConnectionId, aReason);

    if (m_disconnectionHandler)
        m_disconnectionHandler(aConnectionId);

    if (pPlayer)
    {
        if (const auto& cell = pPlayer->GetCellComponent())
        {
            const auto oldCell = cell.Cell;
            pPlayer->SetCellComponent(CellIdComponent{{}, {}, {}});
        }

        m_pWorld->GetDispatcher().trigger(PlayerLeaveEvent(pPlayer));

        m_pWorld->GetPlayerManager().Remove(pPlayer);
    }

    UpdateTitle();
}

void GameServer::InjectMessage(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId)
{
    OnConsume(apData, aSize, aConnectionId);
}

void GameServer::SetLoopbackRoute(ConnectionId_t aConnectionId, std::function<void(const void*, uint32_t)> aCallback)
{
    m_loopbackConnectionId = aConnectionId;
    m_loopbackCallback = std::move(aCallback);
}

void GameServer::SetPlayerCharacter(ConnectionId_t aConnectionId, uint32_t aServerId, const QuestLog& acQuests) noexcept
{
    if (auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId))
    {
        pPlayer->SetCharacter(static_cast<entt::entity>(aServerId));
        pPlayer->GetQuestLogComponent().QuestContent = acQuests;
    }
}

uint32_t GameServer::NewCharacterId() noexcept
{
    return World::ToInteger(m_pWorld->create());
}

void GameServer::FreeCharacterId(uint32_t aServerId) noexcept
{
    if (const auto cEntity = static_cast<entt::entity>(aServerId); m_pWorld->valid(cEntity))
        m_pWorld->destroy(cEntity);
}

void GameServer::OpenLoopback(ConnectionId_t aConnectionId, std::function<void(const void*, uint32_t)> aCallback)
{
    SetLoopbackRoute(aConnectionId, std::move(aCallback));
    m_pWorld->GetPlayerManager().SetHostConnection(aConnectionId);
    OnConnection(aConnectionId);
}

void GameServer::CloseLoopback()
{
    const auto cConnectionId = m_loopbackConnectionId;
    SetLoopbackRoute(0, nullptr);
    if (cConnectionId != 0)
        OnDisconnection(cConnectionId, EDisconnectReason::Quit);
}

void GameServer::SetTestSendCallback(std::function<void(ConnectionId_t, const void*, uint32_t)> aCallback)
{
    m_testSendCallback = std::move(aCallback);
}

void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
{
    Send(aConnectionId, acServerMessage, TiltedPhoques::kReliable);
}

void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage,
                      TiltedPhoques::EPacketFlags aFlags) const
{
    // Recording tap: capture outgoing ServerMessage as NDJSON
    if (m_pRecorder)
        m_pRecorder->RecordServerMessage(aConnectionId, acServerMessage);

    // Loopback intercept: route host messages in-process instead of through enet6 (every send,
    // reliable or not: the host's movement updates are unreliable)
    if (m_loopbackConnectionId != 0 && aConnectionId == m_loopbackConnectionId && m_loopbackCallback)
    {
        Buffer buffer(1 << 20);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // Packet type byte (matches enet6 format)
        acServerMessage.Serialize(writer);
        m_loopbackCallback(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()));
        return;
    }

    // Test mode intercept: capture all outgoing messages without enet6
    if (m_testSendCallback)
    {
        Buffer buffer(1 << 20);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        acServerMessage.Serialize(writer);
        m_testSendCallback(aConnectionId, buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()));
        return;
    }

    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    acServerMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()),
                                     static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnectionId, &packet, aFlags);

    s_allocator.Reset();
}

void GameServer::Send(ConnectionId_t aConnectionId, const ServerAdminMessage& acServerMessage) const
{
    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    acServerMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()),
                                     static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnectionId, &packet);

    s_allocator.Reset();
}

void GameServer::SendToLoaded(const ServerMessage& acServerMessage) const
{
    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer->GetCellComponent())
            pPlayer->Send(acServerMessage);
    }
}

void GameServer::SendToPlayers(const ServerMessage& acServerMessage, const Player* apExcludedPlayer) const
{
    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        if (pPlayer != apExcludedPlayer)
            pPlayer->Send(acServerMessage);
    }
}

static String PrettyPrintModList(const Vector<Mods::Entry>& acMods)
{
    String text;
    for (size_t i = 0; i < acMods.size(); i++)
    {
        text += acMods[i].Filename;
        if (i != (acMods.size() - 1))
            text += ", ";
    }

    return text;
}

void GameServer::AddPlayer(ConnectionId_t aConnectionId, const AuthenticationRequest& acRequest) noexcept
{
    char remoteAddress[48]{"unknown"};
    if (const auto* pPeer = GetPeer(aConnectionId))
        enet_address_get_host_ip(&pPeer->address, remoteAddress, sizeof(remoteAddress));

    Player* pPlayer = m_pWorld->GetPlayerManager().Create(aConnectionId);
    pPlayer->SetEndpoint(remoteAddress);
    pPlayer->SetDiscordId(acRequest.DiscordId);
    pPlayer->SetUsername(acRequest.Username);
    pPlayer->SetLevel(acRequest.Level);

    spdlog::info("New player '{}' [{:x}] connected with {} mods\n\t: {}", pPlayer->GetUsername().c_str(), aConnectionId,
                 acRequest.UserMods.ModList.size(), PrettyPrintModList(acRequest.UserMods.ModList).c_str());
}

void GameServer::Disconnect(ConnectionId_t aConnectionId) noexcept
{
    // What was sent before still arrives: enet disconnects after the queued reliable packets.
    Kick(aConnectionId);
}

GameServer::Uptime GameServer::GetUptime() const noexcept
{
    auto duration = std::chrono::high_resolution_clock::now() - m_startTime;
    auto weeks = std::chrono::duration_cast<std::chrono::weeks>(duration);
    duration -= weeks;
    auto days = std::chrono::duration_cast<std::chrono::days>(duration);
    duration -= days;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    return {weeks.count(), days.count(), hours.count(), minutes.count()};
}

void GameServer::UpdateTitle() const
{
    const auto& name = m_info.name;
    const char* playerText = GetClientCount() <= 1 ? " player" : " players";

    const auto title = fmt::format("{} - {} {} - {} Ticks - {}@{}", name.c_str(), GetClientCount(), playerText, GetTickRate(),
                                   BuildBranch(), BuildCommit());

#if TP_PLATFORM_WINDOWS
    SetConsoleTitleA(title.c_str());
#else
    std::cout << "\033]0;" << title << "\007";
#endif
}

Player* GameServer::GetAdminByUsername(const String& acUsername) const noexcept
{
    for (auto session : m_adminSessions)
    {
        if (auto* pPlayer = PlayerManager::Get()->GetByConnectionId(session))
        {
            if (pPlayer->GetUsername() == acUsername)
                return pPlayer;
        }
    }

    return nullptr;
}

Player const* GameServer::GetAdminByUsername(const String& acUsername) noexcept
{
    for (auto session : m_adminSessions)
    {
        if (auto const* pPlayer = PlayerManager::Get()->GetByConnectionId(session))
        {
            if (pPlayer->GetUsername() == acUsername)
                return pPlayer;
        }
    }

    return nullptr;
}

String GameServer::SanitizeUsername(const String& acUsername) const noexcept
{
    String username = acUsername;

    // space in username handling | "_" -> space
    std::ranges::replace(username, '_', ' ');

    return username;
}

} // namespace Server
