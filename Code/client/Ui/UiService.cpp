#include <TiltedOnlinePCH.h>

#include <Ui/UiService.h>
#include <Ui/UiRenderer.h>

#include <World.h>
#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>

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
        case Rml::Log::LT_ASSERT: spdlog::error("RmlUi: {}", acMessage); break;
        case Rml::Log::LT_WARNING: spdlog::warn("RmlUi: {}", acMessage); break;
        case Rml::Log::LT_INFO: spdlog::info("RmlUi: {}", acMessage); break;
        default: spdlog::debug("RmlUi: {}", acMessage); break;
        }
        return true;
    }

    const std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

UiSystem s_system;

Rml::String UiDirectory() noexcept
{
    if (const char* pOverride = std::getenv("SKYRIMCOOP_UI_DIR"); pOverride && *pOverride)
        return pOverride;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return (std::filesystem::path(path).parent_path() / "Data/SKSE/Plugins/SkyrimCoop/ui").string();
}
} // namespace

UiService::UiService(World& aWorld, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_actorBridge(aActorBridge)
{
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

    DXGI_SWAP_CHAIN_DESC desc{};
    apSwapChain->GetDesc(&desc);
    m_pContext = Rml::CreateContext("game", {int(desc.BufferDesc.Width), int(desc.BufferDesc.Height)});

    if (Rml::DataModelConstructor party = m_pContext->CreateDataModel("party"))
    {
        if (auto member = party.RegisterStruct<Member>())
        {
            member.RegisterMember("name", &Member::Name);
            member.RegisterMember("host", &Member::IsHost);
            member.RegisterMember("level", &Member::Level);
            member.RegisterMember("health", &Member::Health);
        }
        party.RegisterArray<Rml::Vector<Member>>();
        party.Bind("members", &m_members);
        m_partyModel = party.GetModelHandle();
    }

    m_lastChange = LastChange();
    LoadDocuments();
    spdlog::info("UI: RmlUi {} ready, {}x{}, documents from {}", Rml::GetVersion(), desc.BufferDesc.Width, desc.BufferDesc.Height, m_directory);
}

void UiService::LoadDocuments() noexcept
{
    m_pParty = m_pContext->LoadDocument(m_directory + "/party.rml");
    if (m_pParty)
        m_pParty->Show();
    else
        spdlog::error("UI: {}/party.rml did not load", m_directory);
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
    m_pContext->Update();

    m_pRenderer->BeginFrame(cSize.x, cSize.y);
    m_pContext->Render();
    m_pRenderer->EndFrame();
}

void UiService::UpdateParty() noexcept
{
    Rml::Vector<Member> members;
    if (m_world.GetTransport().IsConnected())
    {
        // Each other player's character in our game, for its health.
        Map<uint32_t, uint32_t> characters;
        for (auto [entity, player, formId] : m_world.view<PlayerComponent, FormIdComponent>().each())
            characters[player.Id] = formId.Id;

        for (const auto& [cPlayerId, cPlayer] : m_world.GetSessionService().GetPlayers())
        {
            Member member;
            member.Name = cPlayer.Name.c_str();
            member.IsHost = cPlayer.IsHost;
            member.Level = cPlayer.Level;
            if (const auto it = characters.find(cPlayerId); it != characters.end() && m_actorBridge.ActorExists(it->second))
            {
                const float cMax = m_actorBridge.GetActorPermanentValue(it->second, cHealth);
                if (cMax > 0.f)
                    member.Health = std::clamp(m_actorBridge.GetActorValue(it->second, cHealth) / cMax, 0.f, 1.f);
            }
            members.push_back(std::move(member));
        }
        // The host first, then by name.
        std::sort(members.begin(), members.end(),
                  [](const Member& acLeft, const Member& acRight) { return std::tie(acRight.IsHost, acLeft.Name) < std::tie(acLeft.IsHost, acRight.Name); });
    }

    if (members != m_members)
    {
        m_members = std::move(members);
        m_partyModel.DirtyVariable("members");
    }
}
