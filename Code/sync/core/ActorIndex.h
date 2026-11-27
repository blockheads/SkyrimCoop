#pragma once

#include <NoCopyMove.h>

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

// This game's actors by session id (LocalComponent's, else RemoteComponent's) and by form id (FormIdComponent's),
// kept by the registry's signals. An id on two actors finds the one indexed last. Connected before the services:
// in their on_destroy listeners of these components, the actor is already out.
struct ActorIndex
{
    explicit ActorIndex(entt::registry& aRegistry) noexcept;
    ~ActorIndex();
    NO_COPY_MOVE(ActorIndex);

    // The actor with this session id that has every T; entt::null if none.
    template <class... T> [[nodiscard]] entt::entity BySession(uint32_t aSessionId) const noexcept { return Find<T...>(m_bySession, aSessionId); }
    // The actor with this form id that has every T; entt::null if none.
    template <class... T> [[nodiscard]] entt::entity ByForm(uint32_t aFormId) const noexcept { return Find<T...>(m_byForm, aFormId); }

private:
    using Entities = std::unordered_map<uint32_t, std::vector<entt::entity>>; // newest last
    using Keys = std::unordered_map<entt::entity, uint32_t>;

    template <class... T> [[nodiscard]] entt::entity Find(const Entities& acEntities, uint32_t aId) const noexcept
    {
        const auto cIt = acEntities.find(aId);
        if (cIt == acEntities.end())
            return entt::null;
        if constexpr (sizeof...(T) == 0)
            return cIt->second.back();
        else
        {
            for (auto it = cIt->second.rbegin(); it != cIt->second.rend(); ++it)
                if (m_registry.all_of<T...>(*it))
                    return *it;
            return entt::null;
        }
    }

    static void Set(Entities& aEntities, Keys& aKeys, entt::entity aEntity, std::optional<uint32_t> aId) noexcept;

    void OnSessionSet(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnRemoteRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnFormSet(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnFormRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;

    entt::registry& m_registry;
    Entities m_bySession;
    Entities m_byForm;
    Keys m_sessionOf;
    Keys m_formOf;
};
