
#include <RenderSystemD3D11.h>

#include <DebugService.h>
#include <ImguiService.h>
#include <Ui/UiService.h>
#include <core/World.h>

#include <d3d11.h>

RenderSystemD3D11::RenderSystemD3D11(ImguiService& aImguiService)
    : m_pSwapChain(nullptr)
    , m_imguiService(aImguiService)
{
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

