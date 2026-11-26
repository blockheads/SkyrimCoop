#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <span>

#include <mem/mem.h>

namespace Hooks::vp
{
    // Makes code or data writable while it lives, then restores its protection and flushes the instruction cache.
    class ScopedContext
    {
    public:
        ScopedContext(const mem::pointer aEa, size_t aSize) noexcept
            : m_size(aSize)
            , m_ea(aEa)
        {
            if (!VirtualProtect(aEa.as<void*>(), m_size, PAGE_EXECUTE_READWRITE, reinterpret_cast<DWORD*>(&m_oldProtect)))
                std::abort();
        }

        ~ScopedContext()
        {
            FlushInstructionCache(GetCurrentProcess(), m_ea.as<void*>(), m_size);
            if (!VirtualProtect(m_ea.as<void*>(), m_size, m_oldProtect, reinterpret_cast<DWORD*>(&m_oldProtect)))
                std::abort();
        }

        template <class T> bool Write(const T& acData, const size_t aOffset = 0) const noexcept
        {
            return WriteBuffer(std::span(reinterpret_cast<const uint8_t*>(&acData), sizeof(T)), aOffset);
        }

        bool WriteBuffer(std::span<const uint8_t> aData, size_t aOffset) const noexcept
        {
            if (aData.size() + aOffset > m_size)
                return false;
            std::ranges::copy(aData, (m_ea + aOffset).as<uint8_t*>());
            return true;
        }

    private:
        size_t m_size;
        uint32_t m_oldProtect = 0;
        mem::pointer m_ea;
    };
}
