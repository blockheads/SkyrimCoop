#include <ImguiService.h>

#include <RenderSystemD3D11.h>
#include <d3d11.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui.h>

ImguiService::ImguiService()
    : OnDraw(m_drawSignal)
{
}

ImguiService::~ImguiService() noexcept
{
}


void ImguiService::Create(RenderSystemD3D11* apRenderSystem, HWND aHwnd)
{
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;

    m_imDriver.Initialize(static_cast<void*>(aHwnd));

    // init platform
    if (!ImGui_ImplWin32_Init(aHwnd))
        spdlog::error("Failed to initialize Imgui-Win32");

    apRenderSystem->GetSwapChain()->GetDevice(__uuidof(d3dDevice), reinterpret_cast<void**>(&d3dDevice));
    d3dDevice->GetImmediateContext(&d3dContext);

    ImGui_ImplDX11_Init(d3dDevice, d3dContext);
}

void ImguiService::Render() const
{
    ImGui_ImplDX11_NewFrame();

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    m_drawSignal.publish();
    ImGui::Render();

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

