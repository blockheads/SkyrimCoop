#include <Games/GuestNpcs.h>

#include <atomic>

namespace
{
std::atomic<uint32_t> s_session{0};
uint32_t s_sessions = 0;
} // namespace

void GuestNpcs::SetHolding(bool aOn) noexcept
{
    s_session.store(aOn ? ++s_sessions : 0);
}

uint32_t GuestNpcs::Session() noexcept
{
    return s_session.load(std::memory_order_relaxed);
}
