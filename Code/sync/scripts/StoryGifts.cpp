#include <scripts/StoryGifts.h>

#include <mutex>
#include <unordered_set>

namespace StoryGifts
{
namespace
{
std::mutex s_lock;
std::unordered_set<const void*> s_marked;
thread_local bool t_giving = false;
} // namespace

void Queued(Queue, const void* apFunctor, bool aAddItem, bool aFromStory) noexcept
{
    // Either queue: AddItem defers on the post-render one (re/systems/inventory-changes.md).
    if (!apFunctor)
        return;
    // Not the story's: erase, as a functor at a freed one's address must not keep its mark.
    std::scoped_lock _{s_lock};
    if (aAddItem && aFromStory)
        s_marked.insert(apFunctor);
    else
        s_marked.erase(apFunctor);
}

Run::Run(const void* apFunctor) noexcept
    : m_outer(t_giving)
{
    std::scoped_lock _{s_lock};
    t_giving = s_marked.erase(apFunctor) != 0;
}

Run::~Run()
{
    t_giving = m_outer;
}

bool Giving() noexcept
{
    return t_giving;
}
} // namespace StoryGifts
