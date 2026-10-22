#pragma once

#include <windows.h>

#include <NoCopyMove.h>

namespace Hooks
{
    struct WindowsHook
    {
        NO_COPY_MOVE(WindowsHook);

        void SetCallback(WNDPROC apCallback) noexcept { m_pCallback = apCallback; }
        [[nodiscard]] WNDPROC GetCallback() const noexcept { return m_pCallback; }

        static void Install() noexcept;
        static WindowsHook& Get() noexcept;

    private:

        WindowsHook() noexcept = default;
        ~WindowsHook() noexcept = default;

        WNDPROC m_pCallback{ nullptr };
    };
}
