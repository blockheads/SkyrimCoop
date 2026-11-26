#pragma once

#include <cstdint>

// The story's items (docs/design/party-items.md): a script's AddItem is deferred as an AddItemFunctor and adds in its
// run (56095), when the calling script is gone. A functor queued from a quest's script is marked as it is queued and
// read in its run. Engine-free: the VM's queue hooks (GuestScripts) and the fake engine both call it.
namespace StoryGifts
{
// The VM's queues a native defers its work on: QueueDelayFunctor (53954) and QueuePostRenderCall (53955).
enum class Queue : uint8_t
{
    kDelay,
    kPostRender,
};

// A functor queued: an AddItemFunctor from a quest's script (a quest, alias, dialogue line or scene) is marked.
void Queued(Queue aQueue, const void* apFunctor, bool aAddItem, bool aFromStory) noexcept;

// While a functor runs: Giving() says whether it was the story's (its mark taken).
struct Run
{
    explicit Run(const void* apFunctor) noexcept;
    ~Run();
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

  private:
    bool m_outer;
};
[[nodiscard]] bool Giving() noexcept;
} // namespace StoryGifts
