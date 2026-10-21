#pragma once

struct NotifyGlobals;
struct NotifyQuests;
class GuestEngine;

/**
 * @brief A guest takes the host's globals and quests (HostWorldState) with its engine's own calls
 * (docs/design/world-state-sync.md step 4). Its own story manager starts no quest meanwhile, and its world
 * scripts do not react to its player (step 5).
 */
struct GuestWorldState
{
    GuestWorldState(entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept;
    ~GuestWorldState() noexcept;

    TP_NOCOPYMOVE(GuestWorldState);

private:
    void OnGlobals(const NotifyGlobals& acMessage) noexcept;
    void OnQuests(const NotifyQuests& acMessage) noexcept;

    GuestEngine& m_engine;

    entt::scoped_connection m_globalsConnection;
    entt::scoped_connection m_questsConnection;
};
