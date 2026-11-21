#include <CreatedIds.h>

#include <FormIds.h>

void CreatedIds::SetHosting(bool aHosting) noexcept
{
    std::scoped_lock _{m_lock};
    m_hosting = aHosting;
}

void CreatedIds::Link(uint32_t aFormId, const GameId& acCreated) noexcept
{
    std::scoped_lock _{m_lock};
    m_byForm[aFormId] = acCreated.BaseId;
    m_byId[acCreated.BaseId] = aFormId;
}

void CreatedIds::Unlink(uint32_t aFormId) noexcept
{
    std::scoped_lock _{m_lock};
    if (const auto it = m_byForm.find(aFormId); it != m_byForm.end())
    {
        m_byId.erase(it->second);
        m_byForm.erase(it);
    }
}

void CreatedIds::Clear() noexcept
{
    std::scoped_lock _{m_lock};
    m_byForm.clear();
    m_byId.clear();
}

std::optional<GameId> CreatedIds::ToSession(uint32_t aFormId) const noexcept
{
    if (!IsCreatedForm(aFormId))
        return std::nullopt;
    std::scoped_lock _{m_lock};
    if (m_hosting)
        return CreatedId(aFormId);
    const auto it = m_byForm.find(aFormId);
    return it != m_byForm.end() ? std::optional(CreatedId(0xFF000000 | it->second)) : std::nullopt;
}

uint32_t CreatedIds::ToGame(const GameId& acCreated) const noexcept
{
    if (!IsCreatedId(acCreated))
        return 0;
    std::scoped_lock _{m_lock};
    if (m_hosting)
        return 0xFF000000 | acCreated.BaseId;
    const auto it = m_byId.find(acCreated.BaseId);
    return it != m_byId.end() ? it->second : 0;
}
