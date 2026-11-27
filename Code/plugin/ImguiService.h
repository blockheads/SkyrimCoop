#pragma once

#include <imgui/ImGuiDriver.h>
#include <NoCopyMove.h>

struct RenderSystemD3D9;
struct RenderSystemD3D11;

/**
 * @brief Draws the ImGui UI.
 */
struct ImguiService
{
    using TCallback = void();

    ImguiService();
    ~ImguiService() noexcept;

    NO_COPY_MOVE(ImguiService);

    void Create(RenderSystemD3D11* apRenderSystem, HWND aHwnd);

    void Render() const;
    void Reset() const;

    entt::sink<entt::sigh<TCallback>> OnDraw;

private:
    ImGuiImpl::ImGuiDriver m_imDriver;
    entt::sigh<TCallback> m_drawSignal;
};
