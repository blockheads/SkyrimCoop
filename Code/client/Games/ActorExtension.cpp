#include <ClientPch.h>

#include <Games/ActorExtension.h>

bool ActorExtension::IsCopy() const noexcept
{
    return onlineFlags & kCopy;
}

bool ActorExtension::IsLocal() const noexcept
{
    return !IsCopy();
}

bool ActorExtension::IsPlayer() const noexcept
{
    return onlineFlags & kPlayer;
}

bool ActorExtension::IsOtherPlayer() const noexcept
{
    return IsCopy() && IsPlayer();
}

bool ActorExtension::IsLocalPlayer() const noexcept
{
    return IsLocal() && IsPlayer();
}

void ActorExtension::SetCopy(bool aSet) noexcept
{
    if (aSet)
        onlineFlags |= kCopy;
    else
        onlineFlags &= ~kCopy;
}

void ActorExtension::SetPlayer(bool aSet) noexcept
{
    if (aSet)
        onlineFlags |= kPlayer;
    else
        onlineFlags &= ~kPlayer;
}

uint32_t ActorExtension::CurrentGeneration() noexcept
{
    static const uint32_t s_generation = (GetTickCount() ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s_generation))) | 1u;
    return s_generation;
}

void ActorExtension::EnsureCurrent() noexcept
{
    const uint32_t cCurrent = CurrentGeneration();
    std::atomic_ref<uint32_t> generation(m_generation);
    uint32_t seen = generation.load(std::memory_order_acquire);
    if (seen == cCurrent)
        return;
    // 0 = another thread is resetting it.
    if (seen != 0 && generation.compare_exchange_strong(seen, 0, std::memory_order_acq_rel))
    {
        new (this) ActorExtension;
        generation.store(cCurrent, std::memory_order_release);
        return;
    }
    while (generation.load(std::memory_order_acquire) != cCurrent)
        _mm_pause();
}
