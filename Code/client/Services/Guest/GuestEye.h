#pragma once

#include <Events/Listen.h>

#include <NoCopyMove.h>

#include <vector>

struct NotifyStealthEye;
class IGuestEngine;

/**
 * @brief A guest's stealth eye is what the host's NPCs see of its player (HostEye; stealth.md, "The eye is the host's").
 * Its own game's again once this is gone.
 */
struct GuestEye
{
    GuestEye(entt::dispatcher& aDispatcher, IGuestEngine& aEngine) noexcept;
    ~GuestEye() noexcept;

    NO_COPY_MOVE(GuestEye);

private:
    void OnEye(const NotifyStealthEye& acMessage) noexcept;

    IGuestEngine& m_engine;
    std::vector<entt::scoped_connection> m_connections;
};
