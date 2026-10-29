#pragma once

#include <NoCopyMove.h>

namespace Hooks
{
    struct DInputHook
    {
        NO_COPY_MOVE(DInputHook);

        void SetEnabled(bool aEnabled) noexcept { m_enabled = aEnabled; Update(); }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

        void Acquire() const noexcept;
        void Unacquire() const noexcept;

        static void Install() noexcept;
        static DInputHook& Get() noexcept;

        void Update() const noexcept;

    private:

        DInputHook() noexcept = default;
        ~DInputHook() = default;

        bool m_enabled{ false };
    };
}
