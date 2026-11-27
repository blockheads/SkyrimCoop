#include <Hooks/D3D11Hook.h>

namespace Hooks
{
    D3D11Hook::D3D11Hook() noexcept
    {
    }

    D3D11Hook& D3D11Hook::Get() noexcept
    {
        static D3D11Hook s_instance;
        return s_instance;
    }
}
