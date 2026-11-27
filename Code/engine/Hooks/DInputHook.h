#pragma once

#include <NoCopyMove.h>

namespace Hooks
{
    struct DInputHook
    {
        NO_COPY_MOVE(DInputHook);

        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

        static void Install() noexcept;
        static DInputHook& Get() noexcept;

    private:

        DInputHook() noexcept = default;
        ~DInputHook() = default;

        bool m_enabled{ false };
    };
}
