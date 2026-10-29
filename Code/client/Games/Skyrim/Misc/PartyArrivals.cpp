#include "PartyArrivals.h"

#include <algorithm>

bool PartyArrivals::Arrive(uint32_t aPlayer, uint32_t aLocation, const std::function<bool(uint32_t)>& acIsPlayer) noexcept
{
    std::erase_if(m_at, [&](const auto& acAt) { return !acIsPlayer(acAt.first); });
    const bool cHad = std::ranges::any_of(m_at, [&](const auto& acAt) { return acAt.first != aPlayer && acAt.second == aLocation; });
    m_at[aPlayer] = aLocation;
    return !cHad;
}
