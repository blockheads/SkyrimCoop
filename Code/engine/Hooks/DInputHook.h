#pragma once

#include <NoCopyMove.h>

namespace Hooks
{
    struct DInputHook
    {
        NO_COPY_MOVE(DInputHook);

        static void Install() noexcept;

    private:

        DInputHook() noexcept = default;
        ~DInputHook() = default;
    };
}
