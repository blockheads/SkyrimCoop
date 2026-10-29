#include <Services/BugReportService.h>
#include <Services/DeathService.h>
#include <Events/PlayerDeathEvent.h>
#include <Ui/UiInput.h>
#include <Ui/UiRenderer.h>
#include <Ui/UiService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Events/SessionNoticeEvent.h>
#include <Events/SetTimeCommandEvent.h>
#include <Games/Skyrim/Interface/MenuCursor.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SendChatMessageRequest.h>
#include <Services/JoinService.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>
#include <World.h>

#include <RmlUi/Core.h>

#include <d3d11.h>

namespace
{
constexpr int32_t cHealth = 24; // ActorValueInfo::kHealth

// RmlUi's clock and log.
struct UiSystem final : Rml::SystemInterface
{
    double GetElapsedTime() override
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
    }

    bool LogMessage(Rml::Log::Type aType, const Rml::String& acMessage) override
    {
        switch (aType)
        {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            spdlog::error("RmlUi: {}", acMessage);
            break;
        case Rml::Log::LT_WARNING:
            spdlog::warn("RmlUi: {}", acMessage);
            break;
        case Rml::Log::LT_INFO:
            spdlog::info("RmlUi: {}", acMessage);
            break;
        default:
            spdlog::debug("RmlUi: {}", acMessage);
            break;
        }
        return true;
    }

    const std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

UiSystem s_system;

// How the chat shows its lines: the last ones for a while after they arrive, more while it is open.
constexpr auto cChatLineShown = std::chrono::seconds(10);
constexpr size_t cChatLinesShown = 6;
constexpr size_t cChatLinesOpen = 12;
constexpr size_t cChatHistory = 50;

// The text a key types with the player's keyboard layout and shift (as skymp's InputConverter).
Rml::String TypedText(uint32_t aScanCode, bool aShift) noexcept
{
    const HKL cLayout = GetKeyboardLayout(0);
    const UINT cVirtualKey = MapVirtualKeyExW(aScanCode, MAPVK_VSC_TO_VK_EX, cLayout);
    BYTE state[256]{};
    if (aShift)
        state[VK_SHIFT] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1)
        state[VK_CAPITAL] = 0x01;
    wchar_t typed[8]{};
    const int cCount = ToUnicodeEx(cVirtualKey, aScanCode, state, typed, int(std::size(typed)), 0, cLayout);
    if (cCount <= 0 || typed[0] < 0x20)
        return {};
    char utf8[32]{};
    const int cBytes = WideCharToMultiByte(CP_UTF8, 0, typed, cCount, utf8, int(sizeof(utf8)), nullptr, nullptr);
    return Rml::String(utf8, std::max(cBytes, 0));
}

// Removes the last character (a whole UTF-8 sequence).
void PopCharacter(Rml::String& aText) noexcept
{
    while (!aText.empty() && (uint8_t(aText.back()) & 0xC0) == 0x80)
        aText.pop_back();
    if (!aText.empty())
        aText.pop_back();
}

Rml::String UiDirectory() noexcept
{
    if (const char* pOverride = std::getenv("SKYRIMCOOP_UI_DIR"); pOverride && *pOverride)
        return pOverride;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return (std::filesystem::path(path).parent_path() / "Data/SKSE/Plugins/SkyrimCoop/ui").string();
}
} // namespace

UiService::UiService(World& aWorld, IActorBridge& aActorBridge) noexcept : m_world(aWorld), m_actorBridge(aActorBridge)
{
    Listen<NotifyChatMessageBroadcast, &UiService::OnChatMessage>(m_connections, aWorld.GetDispatcher(), this);
    Listen<SessionNoticeEvent, &UiService::OnSessionNotice>(m_connections, aWorld.GetDispatcher(), this);
    m_party.Visible = false;
    m_party.Opacity = 0.f;
    m_joinButtons.pService = this;
}

UiService::~UiService() noexcept
{
    if (m_initialized)
    {
        // Documents, then the renderer's resources, then the renderer.
        Rml::Shutdown();
    }
    delete m_pRenderer;
}

void UiService::Create(IDXGISwapChain* apSwapChain) noexcept
{
    if (m_initialized || !apSwapChain)
        return;

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pDeviceContext = nullptr;
    if (FAILED(apSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&pDevice))))
    {
        spdlog::error("UI: no D3D11 device");
        return;
    }
    pDevice->GetImmediateContext(&pDeviceContext);
    m_pRenderer = new UiRenderer(pDevice, pDeviceContext);
    pDeviceContext->Release();
    pDevice->Release();
    if (!m_pRenderer->IsReady())
    {
        spdlog::error("UI: the renderer could not be created; no in-game UI");
        return;
    }

    m_pSwapChain = apSwapChain;
    m_directory = UiDirectory();
    Rml::SetSystemInterface(&s_system);
    Rml::SetRenderInterface(m_pRenderer);
    if (!Rml::Initialise())
    {
        spdlog::error("UI: RmlUi did not initialise");
        return;
    }
    m_initialized = true;

    if (!Rml::LoadFontFace(m_directory + "/fonts/Oswald-Variable.ttf"))
        spdlog::error("UI: font not found in {}", m_directory);

    for (const char* pFont : {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"})
        if (!Rml::LoadFontFace(m_directory + "/fonts/" + pFont))
            spdlog::error("UI: font {} not found in {}", pFont, m_directory);

    DXGI_SWAP_CHAIN_DESC desc{};
    apSwapChain->GetDesc(&desc);
    m_pContext = Rml::CreateContext("game", {int(desc.BufferDesc.Width), int(desc.BufferDesc.Height)});

    if (!m_party.Bind(*m_pContext) || !m_chat.Bind(*m_pContext) || !m_join.Bind(*m_pContext) || !m_death.Bind(*m_pContext))
        spdlog::error("UI: data models did not bind");

    m_lastChange = LastChange();
    LoadDocuments();
    spdlog::info("UI: RmlUi {} ready, {}x{}, documents from {}", Rml::GetVersion(), desc.BufferDesc.Width,
                 desc.BufferDesc.Height, m_directory);
}

void UiService::LoadDocuments() noexcept
{
    m_pDeath = m_pContext->LoadDocument(m_directory + "/death.rml");
    if (m_pDeath)
        m_pDeath->Show();
    m_pParty = m_pContext->LoadDocument(m_directory + "/party.rml");
    if (m_pParty)
        m_pParty->Show();
    else
        spdlog::error("UI: {}/party.rml did not load", m_directory);

    m_pChat = m_pContext->LoadDocument(m_directory + "/chat.rml");
    if (m_pChat)
        m_pChat->Show();
    else
        spdlog::error("UI: {}/chat.rml did not load", m_directory);

    m_pJoin = m_pContext->LoadDocument(m_directory + "/join.rml");
    if (m_pJoin)
    {
        m_pJoin->AddEventListener(Rml::EventId::Click, &m_joinButtons);
        m_pJoin->Show();
    }
    else
        spdlog::error("UI: {}/join.rml did not load", m_directory);
}

void UiService::Reload() noexcept
{
    if (!m_initialized)
        return;
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    m_pContext->UnloadAllDocuments();
    m_pContext->Update(); // unloads them
    LoadDocuments();
}

std::filesystem::file_time_type UiService::LastChange() const noexcept
{
    std::error_code error;
    std::filesystem::file_time_type newest{};
    for (const auto& cEntry : std::filesystem::recursive_directory_iterator(m_directory, error))
        if (cEntry.is_regular_file(error))
            newest = std::max(newest, cEntry.last_write_time(error));
    return newest;
}

void UiService::Render() noexcept
{
    if (!m_initialized || !m_pContext)
        return;

    // Edited documents and styles show up by themselves.
    if (const auto cNow = std::chrono::steady_clock::now(); cNow >= m_nextCheck)
    {
        m_nextCheck = cNow + std::chrono::seconds(1);
        if (const auto cChange = LastChange(); cChange != m_lastChange)
        {
            m_lastChange = cChange;
            spdlog::info("UI: files changed, reloading");
            Reload();
        }
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    m_pSwapChain->GetDesc(&desc);
    const Rml::Vector2i cSize{int(desc.BufferDesc.Width), int(desc.BufferDesc.Height)};
    if (cSize != m_pContext->GetDimensions())
        m_pContext->SetDimensions(cSize);
    // Laid out for 1080 lines: the same proportions at any resolution.
    m_pContext->SetDensityIndependentPixelRatio(float(cSize.y) / 1080.f);

    UpdateParty();
    UpdateChat();
    UpdateJoin();
    UpdateDeath();
    m_pContext->Update();

    m_pRenderer->BeginFrame(cSize.x, cSize.y);
    m_pContext->Render();
    m_pRenderer->EndFrame();
}

void UiService::UpdateParty() noexcept
{
    Rml::Vector<Member> members;
    const bool cConnected = m_world.GetTransport().IsConnected();
    if (cConnected)
    {
        // Each other player's character in our game, for its health.
        std::unordered_map<uint32_t, uint32_t> characters;
        for (auto [entity, player, formId] : m_world.view<PlayerComponent, FormIdComponent>().each())
            characters[player.Id] = formId.Id;

        for (const auto& [cPlayerId, cPlayer] : m_world.GetSessionService().GetPlayers())
        {
            Member member;
            member.Name = cPlayer.Name.c_str();
            member.Color = m_playerColors.ForName(member.Name);
            member.IsHost = cPlayer.IsHost;
            member.Level = cPlayer.Level;
            if (const auto it = characters.find(cPlayerId);
                it != characters.end() && m_actorBridge.ActorExists(it->second))
            {
                const float cMax = m_actorBridge.GetActorPermanentValue(it->second, cHealth);
                if (cMax > 0.f)
                    member.Health = std::clamp(m_actorBridge.GetActorValue(it->second, cHealth) / cMax, 0.f, 1.f);
            }
            members.push_back(std::move(member));
        }
        // The host first, then by name.
        std::sort(members.begin(), members.end(), [](const Member& acLeft, const Member& acRight) {
            return std::tie(acRight.IsHost, acLeft.Name) < std::tie(acLeft.IsHost, acRight.Name);
        });
    }

    m_partyActivity.Update(members, cConnected, UiInput::IsHudVisible(), std::chrono::steady_clock::now());
    if (members != m_party.Members)
    {
        m_party.Members = std::move(members);
        m_party.Handle.DirtyVariable("members");
    }
    const float cOpacity = m_partyActivity.Opacity();
    if (m_party.Visible != (cOpacity > 0.f))
    {
        m_party.Visible = cOpacity > 0.f;
        m_party.Handle.DirtyVariable("visible");
    }
    if (m_party.Opacity != cOpacity)
    {
        m_party.Opacity = cOpacity;
        m_party.Handle.DirtyVariable("opacity");
    }
}

void UiService::OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept
{
    ChatLine line{acMessage.PlayerName.c_str(), acMessage.ChatMessage.c_str(), acMessage.MessageType == kSystemMessage};
    if (!line.System)
        line.Color = m_playerColors.ForName(line.Name);
    m_chatHistory.push_back({std::move(line), std::chrono::steady_clock::now()});
    if (m_chatHistory.size() > cChatHistory)
        m_chatHistory.erase(m_chatHistory.begin());
}

Rml::Vector<UiService::ChatLine> UiService::GetChatLines() const noexcept
{
    Rml::Vector<ChatLine> lines;
    for (const auto& cReceived : m_chatHistory)
        lines.push_back(cReceived.Line);
    return lines;
}

namespace
{
// The panel's words for a join on its way; a refusal brings its own (JoinRefusal).
const char* ProgressMessage(JoinStatus::State aState) noexcept
{
    switch (aState)
    {
    case JoinStatus::State::kConnecting: return "Connecting to the host's game...";
    case JoinStatus::State::kChecking: return "Checking your install...";
    case JoinStatus::State::kReceivingWorld: return "Receiving the host's world...";
    case JoinStatus::State::kLoading: return "Loading the world...";
    case JoinStatus::State::kReconnecting: return "Lost connection to the host. Reconnecting...";
    default: return "";
    }
}

bool PanelShows(JoinStatus::State aState) noexcept
{
    switch (aState)
    {
    case JoinStatus::State::kConnecting:
    case JoinStatus::State::kChecking:
    case JoinStatus::State::kReceivingWorld:
    case JoinStatus::State::kLoading:
    case JoinStatus::State::kReconnecting:
    case JoinStatus::State::kRefused:
    case JoinStatus::State::kEnded: return true;
    default: return false;
    }
}

void CopyToClipboard(const Rml::String& acText) noexcept
{
    const int cLength = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    if (cLength <= 0 || !OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    if (HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, cLength * sizeof(wchar_t)))
    {
        MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, static_cast<wchar_t*>(GlobalLock(hText)), cLength);
        GlobalUnlock(hText);
        if (!SetClipboardData(CF_UNICODETEXT, hText))
            GlobalFree(hText);
    }
    CloseClipboard();
}
} // namespace

void UiService::UpdateJoin() noexcept
{
    const auto* pJoinService = m_world.ctx().find<JoinService>();
    if (!pJoinService)
        return;
    const JoinStatus& cStatus = pJoinService->GetStatus();
    if (m_dismissed && *m_dismissed != cStatus)
        m_dismissed.reset();

    Ui::JoinModel next;
    next.Visible = PanelShows(cStatus.Current) && !m_dismissed;
    next.State = ToString(cStatus.Current);
    next.Reason = cStatus.Reason;
    next.Message = cStatus.Message.empty() ? ProgressMessage(cStatus.Current) : cStatus.Message;
    next.Details.assign(cStatus.Details.begin(), cStatus.Details.end());
    next.Progress = cStatus.Current == JoinStatus::State::kReceivingWorld ? cStatus.Progress : -1.f;
    if (next.Visible != m_join.Visible || next.State != m_join.State || next.Reason != m_join.Reason ||
        next.Message != m_join.Message || next.Details != m_join.Details || next.Progress != m_join.Progress)
    {
        next.Handle = m_join.Handle;
        m_join = std::move(next);
        m_join.Handle.DirtyAllVariables();
    }

    // Copy and Back: the menus' cursor and the left button while they show; Escape is Back where no cursor is up.
    const bool cButtons =
        m_join.Visible && (cStatus.Current == JoinStatus::State::kRefused || cStatus.Current == JoinStatus::State::kEnded);
    UiInput::SetButtonsShown(cButtons);
    if (!cButtons)
        return;
    UI* pUI = UI::Get();
    if (const MenuCursor* pCursor = MenuCursor::Get(); pCursor && pUI && pUI->GetMenuOpen("Cursor Menu"))
        m_pContext->ProcessMouseMove(int(pCursor->cursorPosX), int(pCursor->cursorPosY), 0);
    for (const bool cDown : UiInput::TakeClicks())
        cDown ? m_pContext->ProcessMouseButtonDown(0, 0) : m_pContext->ProcessMouseButtonUp(0, 0);
    if (UiInput::TakeEscape())
        OnJoinButton("back");
}

void UiService::OnJoinButton(const Rml::String& acId) noexcept
{
    if (acId == "copy")
        CopyToClipboard(m_join.CopyText());
    else if (acId == "back")
        if (const auto* pJoinService = m_world.ctx().find<JoinService>())
            m_dismissed = pJoinService->GetStatus();
}

void UiService::JoinButtons::ProcessEvent(Rml::Event& aEvent)
{
    for (Rml::Element* pTarget = aEvent.GetTargetElement(); pTarget; pTarget = pTarget->GetParentNode())
        if (!pTarget->GetId().empty())
            return pService->OnJoinButton(pTarget->GetId());
}

void UiService::UpdateChat() noexcept
{
    UiInput::SetChatAvailable(m_pChat && m_world.GetTransport().IsConnected());

    const Rml::String cDraft = m_chat.Draft;
    for (const auto& cKey : UiInput::TakeKeys())
    {
        switch (cKey.ScanCode)
        {
        case UiInput::cEnter:
        case UiInput::cNumpadEnter:
            SendChat();
            UiInput::CloseChat();
            break;
        case UiInput::cEscape:
            UiInput::CloseChat();
            break;
        case UiInput::cBackspace:
            PopCharacter(m_chat.Draft);
            break;
        default:
            if (m_chat.Draft.size() < kChatMaxLength)
                m_chat.Draft += TypedText(cKey.ScanCode, cKey.Shift);
            break;
        }
    }

    const bool cOpen = UiInput::IsChatOpen();
    if (!cOpen)
        m_chat.Draft.clear();
    if (cOpen != m_chat.Open)
    {
        m_chat.Open = cOpen;
        m_chat.Handle.DirtyVariable("open");
    }
    if (m_chat.Draft != cDraft)
        m_chat.Handle.DirtyVariable("draft");

    // Open: the last lines. Closed: the last few, while they are recent.
    Rml::Vector<ChatLine> shown;
    const auto cNow = std::chrono::steady_clock::now();
    const size_t cCount = m_chat.Open ? cChatLinesOpen : cChatLinesShown;
    for (size_t i = m_chatHistory.size() > cCount ? m_chatHistory.size() - cCount : 0; i < m_chatHistory.size(); ++i)
        if (m_chat.Open || cNow - m_chatHistory[i].At < cChatLineShown)
            shown.push_back(m_chatHistory[i].Line);
    if (shown != m_chat.Lines)
    {
        m_chat.Lines = std::move(shown);
        m_chat.Handle.DirtyVariable("lines");
    }
}

void UiService::RunChatCommand(const Rml::String& acLine) noexcept
{
    if (acLine == "/bug" || acLine.starts_with("/bug "))
    {
        m_world.ctx().at<BugReportService>().Report(acLine.size() > 5 ? acLine.substr(5) : "");
        return;
    }
    if (acLine == "/export")
    {
        m_world.ctx().at<BugReportService>().Export();
        return;
    }
    char command[16]{};
    int hours = -1, minutes = -1;
    const int cRead = std::sscanf(acLine.c_str(), "/%15s %d %d", command, &hours, &minutes);
    if (cRead >= 1 && std::strcmp(command, "settime") == 0)
    {
        if (cRead == 3 && hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60)
            m_world.GetDispatcher().trigger(SetTimeCommandEvent(
                static_cast<uint8_t>(hours), static_cast<uint8_t>(minutes), m_world.GetTransport().GetLocalPlayerId()));
        else
            ShowSystemLine("Usage: /settime <hours 0-23> <minutes 0-59>");
        return;
    }
    ShowSystemLine("Commands: /bug <what happened>, /export, /settime <hours> <minutes>");
}

void UiService::OnSessionNotice(const SessionNoticeEvent& acNotice) noexcept
{
    ShowSystemLine(acNotice.Text, acNotice.Perished);
}

void UiService::ShowSystemLine(const Rml::String& acText, bool aPerished) noexcept
{
    ChatLine line{"", acText, true};
    line.Perished = aPerished;
    m_chatHistory.push_back({std::move(line), std::chrono::steady_clock::now()});
    if (m_chatHistory.size() > cChatHistory)
        m_chatHistory.erase(m_chatHistory.begin());
}

void UiService::SendChat() noexcept
{
    if (m_chat.Draft.empty())
        return;
    if (m_chat.Draft[0] == '/')
    {
        RunChatCommand(m_chat.Draft);
        m_chat.Draft.clear();
        return;
    }
    // Everyone gets it from the host, us too: what shows is what was sent.
    SendChatMessageRequest request;
    request.ChatMessage = m_chat.Draft.c_str();
    m_world.GetTransport().Send(request);
    m_chat.Draft.clear();
}

void UiService::UpdateDeath() noexcept
{
    const auto* cpDeath = m_world.ctx().find<DeathService>();
    const bool cDown = cpDeath && cpDeath->IsDown();
    m_death.Visible = cDown && !UiInput::GetHudVisibility().BlockingMenu;
    m_death.Name = cpDeath ? cpDeath->SpectatingName() : "";
    m_death.Status = cpDeath ? cpDeath->Status() : "";
    UiInput::SetSpectatorAvailable(cDown && cpDeath->Spectating());
    if (m_death.Handle)
        m_death.Handle.DirtyAllVariables();
}
