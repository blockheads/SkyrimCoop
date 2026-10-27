#include <Followers/Anchors.h>
#include <catch2/catch.hpp>

using namespace Followers;

TEST_CASE("Followers follow the requested member and resolve replacement copies by stable key", "[followers]")
{
    Anchors anchors;
    anchors.Follow(0xA2C94, "guest");
    std::vector<Member> members{{"host", 1, true, true, {}}, {"guest", 2, false, true, {}}};
    CHECK(anchors.Choose(0xA2C94, {}, members) == 2);
    members[1].Character = 99;
    CHECK(anchors.Choose(0xA2C94, {}, members) == 99);
    anchors.Follow(0xA2C94, "host");
    CHECK(anchors.Choose(0xA2C94, {}, members) == 1);
}

TEST_CASE("Unavailable follower anchors fall back to the standing host and return when available", "[followers]")
{
    Anchors anchors;
    anchors.Follow(50, "guest");
    std::vector<Member> members{
        {"host", 1, true, true, {}}, {"guest", 2, false, false, {}}, {"other", 3, false, true, {}}};
    CHECK(anchors.Choose(50, {}, members) == 1);
    CHECK(anchors.Desired(50) == "guest");
    members.erase(members.begin() + 1);
    CHECK(anchors.Choose(50, {}, members) == 1);
    members.push_back({"guest", 20, false, true, {}});
    CHECK(anchors.Choose(50, {}, members) == 20);
}

TEST_CASE("With the host down followers choose the nearest standing member with stable key ties", "[followers]")
{
    Anchors anchors;
    anchors.Follow(50, "away");
    const Place origin{1, 10, {0, 0, 0}};
    std::vector<Member> members{{"host", 1, true, false, origin},
                                {"z", 2, false, true, {1, 11, {20, 0, 0}}},
                                {"a", 3, false, true, {1, 12, {100, 0, 0}}},
                                {"other-world", 4, false, true, {2, 10, {0, 0, 0}}}};
    CHECK(anchors.Choose(50, origin, members) == 2);
    members[2].At.Position = {-20, 0, 0};
    CHECK(anchors.Choose(50, origin, members) == 3);
    for (auto& member : members)
        member.Standing = false;
    CHECK(anchors.Choose(50, origin, members) == 0);
}

TEST_CASE("A follow order during fallback replaces the original desired anchor", "[followers]")
{
    Anchors anchors;
    anchors.Follow(50, "away");
    std::vector<Member> members{{"host", 1, true, true, {}}, {"other", 3, false, true, {}}};
    CHECK(anchors.Choose(50, {}, members) == 1);
    anchors.Follow(50, "other");
    members.push_back({"away", 2, false, true, {}});
    CHECK(anchors.Choose(50, {}, members) == 3);
}

#include <Followers/Records.h>

TEST_CASE("Follower saves preserve desired keys and resolve host references without saving temporary targets",
          "[followers]")
{
    Anchors anchors;
    anchors.Follow(50, "away");
    anchors.Follow(60, "other");
    Anchors loaded;
    REQUIRE(Decode(loaded, Encode(anchors), 1, [](uint32_t aOld, uint32_t& aNew) {
        aNew = aOld + 100;
        return aOld != 60;
    }));
    CHECK(loaded.Desired(150) == "away");
    CHECK(loaded.All().size() == 1);
    CHECK_FALSE(Decode(loaded, Encode(anchors), 2));
    CHECK(loaded.All().empty());
    CHECK_FALSE(Decode(loaded, Encode(anchors).substr(0, 3), 1));
    CHECK(loaded.All().empty());
}

TEST_CASE("Clearing vanilla follower aliases forgets only dismissed or dead followers", "[followers]")
{
    Anchors anchors;
    anchors.Follow(50, "guest");
    anchors.Follow(60, "host");
    const std::array<uint32_t, 1> active{60};
    anchors.Retain(active);
    CHECK(anchors.Desired(50).empty());
    CHECK(anchors.Desired(60) == "host");
}
