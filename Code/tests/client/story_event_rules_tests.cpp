// A story event of any type between the engine and the session (Misc/StoryEventRules).
#include <catch2/catch.hpp>

#include <Misc/StoryEventRules.h>

#include <cstring>

using namespace StoryEventRules;
using Member = StoryEvent::Member;

namespace
{
constexpr uint32_t kPlayerHandle = 0x100, kNpcHandle = 0x200, kChestHandle = 0x300;

Named Reference(uint32_t aHandle)
{
    switch (aHandle)
    {
    case kPlayerHandle: return {Named::kThisPlayer};
    case kNpcHandle: return {Named::kActor};
    case kChestHandle: return {Named::kObject, GameId(0, 0x1A001)};
    default: return {};
    }
}

std::optional<GameId> Form(uint64_t aForm)
{
    return GameId(0, static_cast<uint32_t>(aForm & 0xFFFFFF));
}

template <class T> T At(const Raw& acRaw, size_t aOffset)
{
    T value{};
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(acRaw.data()) + aOffset, sizeof(T));
    return value;
}
} // namespace

TEST_CASE("A raw event is laid out as the engine's build reads it", "[story]")
{
    // CLOC: a 4-byte reference, then two 8-byte pointers from the next 8-aligned offset.
    const std::array cLocation{MemberType::kReference, MemberType::kLocation, MemberType::kLocation};
    const auto cRaw = Layout(cLocation, std::array<uint64_t, 3>{0x100, 0x7FF611111111, 0x7FF622222222});
    REQUIRE(cRaw);
    CHECK(At<uint32_t>(*cRaw, 0) == 0x100);
    CHECK(At<uint64_t>(*cRaw, 8) == 0x7FF611111111);
    CHECK(At<uint64_t>(*cRaw, 16) == 0x7FF622222222);

    // KILL: two references share 8 bytes, the location after them, then two integers packed.
    const std::array cKill{MemberType::kReference, MemberType::kReference, MemberType::kLocation, MemberType::kInteger,
                           MemberType::kInteger};
    const auto cKillRaw = Layout(cKill, std::array<uint64_t, 5>{1, 2, 0x7FF633333333, 4, 5});
    REQUIRE(cKillRaw);
    CHECK(At<uint32_t>(*cKillRaw, 0) == 1);
    CHECK(At<uint32_t>(*cKillRaw, 4) == 2);
    CHECK(At<uint64_t>(*cKillRaw, 8) == 0x7FF633333333);
    CHECK(At<uint32_t>(*cKillRaw, 16) == 4);
    CHECK(At<uint32_t>(*cKillRaw, 20) == 5);

    CHECK_FALSE(Layout(cLocation, std::array<uint64_t, 4>{}));
}

TEST_CASE("A guest's event names its player, references and forms for the session", "[story]")
{
    const std::array cCraft{MemberType::kReference, MemberType::kLocation, MemberType::kForm, MemberType::kInteger};
    const auto cSent = ToSend(cCraft, std::array<uint64_t, 4>{kChestHandle, 0, 0x7FF6001397E, 3}, Reference, Form);
    REQUIRE(cSent);
    CHECK(*cSent == std::vector<Member>{{Member::kReference, GameId(0, 0x1A001)}, {}, {Member::kForm, GameId(0, 0x1397E)},
                                        {Member::kInteger, {}, 3}});

    const std::array cArrival{MemberType::kReference, MemberType::kLocation, MemberType::kLocation};
    const auto cArrived = ToSend(cArrival, std::array<uint64_t, 3>{kPlayerHandle, 0, 0}, Reference, Form);
    REQUIRE(cArrived);
    CHECK((*cArrived)[0].Type == Member::kCharacter);
}

TEST_CASE("An event naming another game's actor is that game's to raise", "[story]")
{
    const std::array cKill{MemberType::kReference, MemberType::kReference};
    CHECK_FALSE(ToSend(cKill, std::array<uint64_t, 2>{kNpcHandle, kPlayerHandle}, Reference, Form));
}

TEST_CASE("A member fits its type on the host only as the same kind", "[story]")
{
    CHECK(Fits(MemberType::kReference, Member::kCharacter));
    CHECK(Fits(MemberType::kReference, Member::kReference));
    CHECK_FALSE(Fits(MemberType::kReference, Member::kForm));
    CHECK(Fits(MemberType::kKeyword, Member::kForm));
    CHECK_FALSE(Fits(MemberType::kInteger, Member::kForm));
    CHECK(Fits(MemberType::kInteger, Member::kNone));
    CHECK(TypeName(StoryEvent::TypeOf("CLOC")) == "CLOC");
}
