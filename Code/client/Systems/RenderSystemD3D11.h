#pragma once


struct ImguiService;
struct IDXGISwapChain;

/**
 * @brief Runs the renderer for D3D11.
 */
struct RenderSystemD3D11
{
    explicit RenderSystemD3D11(ImguiService& aImguiService);
    ~RenderSystemD3D11();

    TP_NOCOPYMOVE(RenderSystemD3D11);

    [[nodiscard]] HWND GetWindow() const;
    [[nodiscard]] IDXGISwapChain* GetSwapChain() const;

    // to make yamashi mad
    // The game created its device (aArrangeWindows: tile the test games' windows), or a hot
    // reloaded DLL found it already made (its windows were arranged by the first load).
    void OnDeviceCreation(IDXGISwapChain* apSwapChain, bool aArrangeWindows = true);
    void OnRender();
    void OnReset(IDXGISwapChain* apSwapChain);

private:
    IDXGISwapChain* m_pSwapChain;
    ImguiService& m_imguiService;
    size_t m_createConnection;
    size_t m_renderConnection;
    size_t m_resetConnection;
};

