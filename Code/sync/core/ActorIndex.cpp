#include <core/ActorIndex.h>

#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>

#include <algorithm>

ActorIndex::ActorIndex(entt::registry& aRegistry) noexcept
    : m_registry(aRegistry)
{
    // emplace_or_replace on an actor that has one already updates it (on_update), not on_construct.
    aRegistry.on_construct<LocalComponent>().connect<&ActorIndex::OnSessionSet>(this);
    aRegistry.on_update<LocalComponent>().connect<&ActorIndex::OnSessionSet>(this);
    aRegistry.on_destroy<LocalComponent>().connect<&ActorIndex::OnLocalRemoved>(this);
    aRegistry.on_construct<RemoteComponent>().connect<&ActorIndex::OnSessionSet>(this);
    aRegistry.on_update<RemoteComponent>().connect<&ActorIndex::OnSessionSet>(this);
    aRegistry.on_destroy<RemoteComponent>().connect<&ActorIndex::OnRemoteRemoved>(this);
    aRegistry.on_construct<FormIdComponent>().connect<&ActorIndex::OnFormSet>(this);
    aRegistry.on_update<FormIdComponent>().connect<&ActorIndex::OnFormSet>(this);
    aRegistry.on_destroy<FormIdComponent>().connect<&ActorIndex::OnFormRemoved>(this);
}

ActorIndex::~ActorIndex()
{
    m_registry.on_construct<LocalComponent>().disconnect(this);
    m_registry.on_update<LocalComponent>().disconnect(this);
    m_registry.on_destroy<LocalComponent>().disconnect(this);
    m_registry.on_construct<RemoteComponent>().disconnect(this);
    m_registry.on_update<RemoteComponent>().disconnect(this);
    m_registry.on_destroy<RemoteComponent>().disconnect(this);
    m_registry.on_construct<FormIdComponent>().disconnect(this);
    m_registry.on_update<FormIdComponent>().disconnect(this);
    m_registry.on_destroy<FormIdComponent>().disconnect(this);
}

void ActorIndex::Set(Entities& aEntities, Keys& aKeys, entt::entity aEntity, std::optional<uint32_t> aId) noexcept
{
    const auto cOld = aKeys.find(aEntity);
    if (cOld != aKeys.end())
    {
        if (aId == cOld->second)
            return;
        auto& entities = aEntities[cOld->second];
        std::erase(entities, aEntity);
        if (entities.empty())
            aEntities.erase(cOld->second);
        aKeys.erase(cOld);
    }
    if (!aId)
        return;
    aEntities[*aId].push_back(aEntity);
    aKeys[aEntity] = *aId;
}

// The session id is the Local's while it has one (Utils::GetSessionId).
void ActorIndex::OnSessionSet(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* cpLocal = aRegistry.try_get<LocalComponent>(aEntity);
    Set(m_bySession, m_sessionOf, aEntity, cpLocal ? cpLocal->Id : aRegistry.get<RemoteComponent>(aEntity).Id);
}

// Signalled before the component goes: what is left is the other one.
void ActorIndex::OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* cpRemote = aRegistry.try_get<RemoteComponent>(aEntity);
    Set(m_bySession, m_sessionOf, aEntity, cpRemote ? std::optional{cpRemote->Id} : std::nullopt);
}

void ActorIndex::OnRemoteRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* cpLocal = aRegistry.try_get<LocalComponent>(aEntity);
    Set(m_bySession, m_sessionOf, aEntity, cpLocal ? std::optional{cpLocal->Id} : std::nullopt);
}

void ActorIndex::OnFormSet(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    Set(m_byForm, m_formOf, aEntity, aRegistry.get<FormIdComponent>(aEntity).Id);
}

void ActorIndex::OnFormRemoved(entt::registry&, entt::entity aEntity) noexcept
{
    Set(m_byForm, m_formOf, aEntity, std::nullopt);
}
