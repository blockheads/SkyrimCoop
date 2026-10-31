#include <catch2/catch.hpp>
#include <Games/ActorPools.h>
#include <map>
#include <algorithm>

namespace
{
struct Snapshot
{
    std::map<uint32_t, float> ActorValuesList;
    std::map<uint32_t, float> ActorMaxValuesList;
    std::map<uint32_t, float> ActorTemporaryValuesList;
};

struct Pool
{
    float Permanent = 175.f;
    float Temporary = 0.f;
    float Current = 175.f;

    void Restore(const Snapshot& acValues)
    {
        ActorPools::Restore(acValues, [&](int32_t) { return Current; }, [&](int32_t) { return Temporary; },
            [&](int32_t aMode, int32_t, float aTarget) {
                if (aMode == ActorPools::kPermanent)
                {
                    Current += aTarget - Permanent;
                    Permanent = aTarget;
                }
                else if (aMode == ActorPools::kTemporary)
                {
                    Temporary += aTarget - Current;
                    Current = aTarget;
                }
                else
                    Current = std::min(aTarget, Permanent + Temporary);
            });
    }
};
}

TEST_CASE("A fortified pool preserves its permanent value and enchantment independently", "[actor-pools]")
{
    Pool pool;
    pool.Restore({{{25, 205.f}}, {{25, 175.f}}, {{25, 30.f}}});
    CHECK(pool.Permanent == 175.f);
    CHECK(pool.Temporary == 30.f);
    CHECK(pool.Current == 205.f);
    SECTION("spending magicka retains its fortified maximum")
    {
        pool.Restore({{{25, 140.f}}, {{25, 175.f}}, {{25, 30.f}}});
        CHECK(pool.Temporary == 30.f);
        CHECK(pool.Current == 140.f);
    }
    SECTION("unequipping the enchantment removes its modifier")
    {
        pool.Restore({{{25, 175.f}}, {{25, 175.f}}, {{25, 0.f}}});
        CHECK(pool.Temporary == 0.f);
        CHECK(pool.Current == 175.f);
    }
    SECTION("reapplying a snapshot cannot accumulate the enchantment")
    {
        pool.Restore({{{25, 205.f}}, {{25, 175.f}}, {{25, 30.f}}});
        CHECK(pool.Temporary == 30.f);
        CHECK(pool.Current == 205.f);
    }
}

TEST_CASE("A debuffed pool restores a lower effective maximum", "[actor-pools]")
{
    Pool pool;
    pool.Restore({{{25, 120.f}}, {{25, 175.f}}, {{25, -25.f}}});
    CHECK(pool.Permanent == 175.f);
    CHECK(pool.Temporary == -25.f);
    CHECK(pool.Current == 120.f);
}
