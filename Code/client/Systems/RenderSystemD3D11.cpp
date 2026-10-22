
#include <TiltedOnlinePCH.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/DebugService.h>
#include <Services/ImguiService.h>
#include <Ui/UiService.h>
#include <World.h>

#include <Hooks/D3D11Hook.h>
#include <d3d11.h>

RenderSystemD3D11::RenderSystemD3D11(ImguiService& aImguiService)
    : m_pSwapChain(nullptr)
    , m_imguiService(aImguiService)
{
    auto& d3d11 = Hooks::D3D11Hook::Get();

    m_createConnection = d3d11.OnCreate.Connect([this](IDXGISwapChain* apSwapChain) { OnDeviceCreation(apSwapChain); });
    m_resetConnection = d3d11.OnLost.Connect(std::bind(&RenderSystemD3D11::OnReset, this, std::placeholders::_1));
}

RenderSystemD3D11::~RenderSystemD3D11()
{
    auto& d3d11 = Hooks::D3D11Hook::Get();

    d3d11.OnCreate.Disconnect(m_createConnection);
    d3d11.OnLost.Disconnect(m_resetConnection);
}

HWND RenderSystemD3D11::GetWindow() const
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.OutputWindow = nullptr;

    if (m_pSwapChain)
        m_pSwapChain->GetDesc(&desc);

    return desc.OutputWindow;
}

IDXGISwapChain* RenderSystemD3D11::GetSwapChain() const
{
    return m_pSwapChain;
}

void RenderSystemD3D11::OnDeviceCreation(IDXGISwapChain* apSwapChain, bool aArrangeWindows)
{
    m_pSwapChain = apSwapChain;

    m_imguiService.Create(this, GetWindow());
    if (auto* pUi = World::Get().ctx().find<UiService>())
        pUi->Create(apSwapChain);
    // Not again after a hot reload: its "another game is open" mutex is our own process's by then,
    // and it would move this window to the second game's place.
    if (aArrangeWindows)
        DebugService::ArrangeGameWindows(GetWindow());
}

void RenderSystemD3D11::OnRender()
{
    // Under ImGui: the debug views stay on top.
    if (auto* pUi = World::Get().ctx().find<UiService>())
        pUi->Render();
    m_imguiService.Render();
}

void RenderSystemD3D11::OnReset(IDXGISwapChain* apSwapChain)
{
    m_pSwapChain = apSwapChain;

    m_imguiService.Reset();
}

