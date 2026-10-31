#include <Papyrus/StoryGifts.h>

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
    if (!apFunctor || !aAddItem || !aFromStory)
        return;
    std::scoped_lock _{s_lock};
    s_marked.insert(apFunctor);
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
