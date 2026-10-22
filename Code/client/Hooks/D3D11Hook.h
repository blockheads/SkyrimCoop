#pragma once

#include <Hooks/Signal.h>

struct IDXGISwapChain;

namespace Hooks
{
    struct D3D11Hook
    {
        NO_COPY_MOVE(D3D11Hook);

        Signal<void(IDXGISwapChain*)> OnCreate;
        Signal<void(IDXGISwapChain*)> OnPresent;
        Signal<void(IDXGISwapChain*)> OnLost;

        static void Install() noexcept;
        static D3D11Hook& Get() noexcept;

    private:

        D3D11Hook() noexcept;
        ~D3D11Hook() = default;
    };
}
