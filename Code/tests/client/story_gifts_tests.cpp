// The story's items: an AddItem queued from a quest's script is the story's in its run (Papyrus/StoryGifts.h).
#include <catch2/catch.hpp>

#include <Papyrus/StoryGifts.h>

using StoryGifts::Queue;

TEST_CASE("An AddItem queued from a quest's script is the story's in its run, on either queue", "[StoryGifts]")
{
    const int cFunctor = 0;
    // The AddItem native defers on the post-render queue (53955), as MoveTo; other natives use the delay queue.
    for (const Queue cQueue : {Queue::kPostRender, Queue::kDelay})
    {
        StoryGifts::Queued(cQueue, &cFunctor, true, true);
        {
            const StoryGifts::Run cRun(&cFunctor);
            CHECK(StoryGifts::Giving());
        }
        CHECK_FALSE(StoryGifts::Giving());
        const StoryGifts::Run cAgain(&cFunctor); // the mark is taken by its run
        CHECK_FALSE(StoryGifts::Giving());
    }
}

TEST_CASE("Not the story's: another functor, or an AddItem from an object's own script", "[StoryGifts]")
{
    const int cFunctor = 0;
    StoryGifts::Queued(Queue::kPostRender, &cFunctor, false, true);
    StoryGifts::Queued(Queue::kPostRender, &cFunctor, true, false);
    const StoryGifts::Run cRun(&cFunctor);
    CHECK_FALSE(StoryGifts::Giving());
}
