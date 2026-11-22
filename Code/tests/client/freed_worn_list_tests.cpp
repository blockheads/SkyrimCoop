#include <catch2/catch.hpp>

#include <Games/FreedWornList.h>

TEST_CASE("A worn list the unequip freed is not handed on to RemoveItem", "[inventory][freed-worn-list]")
{
    FreedWornList freed;
    int list = 0, other = 0;
    freed.Enter(); // the RemoveItem that unequips

    SECTION("freed: every nested RemoveItem sees it as freed, until the outer one returns")
    {
        freed.Unequipped(&list, false);
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
    SECTION("still held: handed on")
    {
        freed.Unequipped(&list, true);
        freed.Enter();
        CHECK_FALSE(freed.IsFreed(&list));
    }
    SECTION("another list, or none, is not the freed one")
    {
        freed.Unequipped(&list, false);
        freed.Enter();
        CHECK_FALSE(freed.IsFreed(&other));
        CHECK_FALSE(freed.IsFreed(nullptr));
        CHECK(freed.IsFreed(&list));
    }
    SECTION("a later unequip that kept its list clears an earlier freed one")
    {
        freed.Unequipped(&list, false);
        freed.Unequipped(&other, true);
        CHECK_FALSE(freed.IsFreed(&list));
    }
}
