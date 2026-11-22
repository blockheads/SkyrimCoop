#include <catch2/catch.hpp>

#include <Games/FreedWornList.h>

#include <vector>

TEST_CASE("A worn list the unequip freed is not handed on to RemoveItem", "[inventory][freed-worn-list]")
{
    FreedWornList freed;
    int list = 0, twin = 0, other = 0;
    using Lists = std::vector<const void*>;
    freed.Enter(); // the RemoveItem that unequips

    SECTION("merged into its equal twin: every nested RemoveItem sees it as freed, until the outer one returns")
    {
        freed.Unequipped(&list, Lists{&twin});
        for (int nested = 0; nested < 2; ++nested) // 16059 calls itself twice with the same list (0x14022f51d, 0x1402305e0)
        {
            freed.Enter();
            CHECK(freed.IsFreed(&list));
            freed.Leave();
        }
        freed.Leave();
        freed.Enter(); // a later removal: the address may be a new list's
        CHECK_FALSE(freed.IsFreed(&list));
    }
    SECTION("beside an unequal list it is still the entry's: handed on")
    {
        freed.Unequipped(&list, Lists{&list, &other});
        freed.Enter();
        CHECK_FALSE(freed.IsFreed(&list));
    }
    SECTION("another list, or none, is not the freed one")
    {
        freed.Unequipped(&list, Lists{});
        freed.Enter();
        CHECK_FALSE(freed.IsFreed(&other));
        CHECK_FALSE(freed.IsFreed(nullptr));
        CHECK(freed.IsFreed(&list));
    }
    SECTION("a nested removal's own unequip between the two calls keeps the outer one's freed list")
    {
        freed.Unequipped(&list, Lists{&twin});
        freed.Enter(); // the first nested call unequips a second worn stack, which survives
        freed.Unequipped(&other, Lists{&other});
        freed.Leave();
        freed.Enter(); // the second nested call, handed the first list again
        CHECK(freed.IsFreed(&list));
    }
    SECTION("deeper than tracked: nothing is remembered, nothing breaks")
    {
        for (int depth = 0; depth < 12; ++depth)
            freed.Enter();
        freed.Unequipped(&list, Lists{});
        CHECK_FALSE(freed.IsFreed(&list));
        for (int depth = 0; depth < 12; ++depth)
            freed.Leave();
        CHECK_FALSE(freed.IsFreed(&list));
    }
}
