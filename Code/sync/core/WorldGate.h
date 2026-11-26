#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

// Hooks that read the World on engine job threads, against the cog swap taking it down (harness/HotReload/PluginUnload): a
// reader admitted before Close keeps the World until it leaves; none is admitted while there is no World.
namespace WorldGate
{
inline std::atomic<bool> g_open{false};
inline std::atomic<uint32_t> g_readers{0};

struct Reader
{
    Reader() noexcept
    {
        g_readers.fetch_add(1);
        m_admitted = g_open.load();
        if (!m_admitted)
            g_readers.fetch_sub(1);
    }
    ~Reader()
    {
        if (m_admitted)
            g_readers.fetch_sub(1);
    }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    explicit operator bool() const noexcept { return m_admitted; }

  private:
    bool m_admitted;
};

// CreateWorld (SkyrimEngine.cpp).
inline void Open() noexcept
{
    g_open.store(true);
}
// Before the World goes: no new reader, and the ones inside have left.
inline void Close() noexcept
{
    g_open.store(false);
    while (g_readers.load())
        std::this_thread::yield();
}
} // namespace WorldGate
