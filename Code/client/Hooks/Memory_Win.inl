
namespace Hooks::vp
{
    ScopedContext::ScopedContext(const mem::pointer aEa, const size_t aSize) noexcept
        : m_size(aSize)
        , m_oldProtect(0)
        , m_ea(aEa)
    {
        if (!VirtualProtect(aEa.as<void*>(), m_size, PAGE_EXECUTE_READWRITE, reinterpret_cast<DWORD*>(&m_oldProtect)))
            std::abort();
    }

    ScopedContext::~ScopedContext()
    {
        FlushInstructionCache(GetCurrentProcess(), m_ea.as<void*>(), m_size);
        if (!VirtualProtect(m_ea.as<void*>(), m_size, m_oldProtect, reinterpret_cast<DWORD*>(&m_oldProtect)))
            std::abort();
    }

    bool ScopedContext::WriteBuffer(const uint8_t* acpData, const size_t aSize, const size_t aOffset) const noexcept
    {
        if (aSize + aOffset > m_size)
            return false;

        auto memory = m_ea + aOffset;
        std::copy(acpData, acpData + aSize, memory.as<uint8_t*>());

        return true;
    }
}
