#include <Bridges/PlacedByGame.h>

#include <mutex>
#include <unordered_set>

namespace
{
std::mutex s_lock;
std::unordered_set<uint32_t> s_placed;
} // namespace

namespace PlacedByGame
{
void Mark(uint32_t aFormId) noexcept
{
    std::scoped_lock _(s_lock);
    s_placed.insert(aFormId);
}

bool Is(uint32_t aFormId) noexcept
{
    std::scoped_lock _(s_lock);
    return s_placed.contains(aFormId);
}

void Clear(uint32_t aFormId) noexcept
{
    std::scoped_lock _(s_lock);
    s_placed.erase(aFormId);
}

void ClearAll() noexcept
{
    std::scoped_lock _(s_lock);
    s_placed.clear();
}
} // namespace PlacedByGame
