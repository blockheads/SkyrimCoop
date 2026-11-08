#include <Games/HiddenCopies.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace
{
std::mutex s_lock;
std::vector<uint32_t> s_hidden;
std::atomic<bool> s_any{false}; // the hooks' fast path: nothing hidden out of a session
} // namespace

void HiddenCopies::Set(uint32_t aFormId, bool aHidden) noexcept
{
    std::scoped_lock lock(s_lock);
    const auto it = std::find(s_hidden.begin(), s_hidden.end(), aFormId);
    if (aHidden && it == s_hidden.end())
        s_hidden.push_back(aFormId);
    else if (!aHidden && it != s_hidden.end())
        s_hidden.erase(it);
    s_any.store(!s_hidden.empty(), std::memory_order_release);
}

bool HiddenCopies::Contains(uint32_t aFormId) noexcept
{
    if (!s_any.load(std::memory_order_acquire))
        return false;
    std::scoped_lock lock(s_lock);
    return std::find(s_hidden.begin(), s_hidden.end(), aFormId) != s_hidden.end();
}
