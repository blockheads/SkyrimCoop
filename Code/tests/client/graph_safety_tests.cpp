#include <catch2/catch.hpp>

#include <Games/Animation/GraphReference.h>
#include <Games/Animation/VariableSync.h>

#include <array>
#include <bit>

TEST_CASE("Graph synchronization respects the current layout and scalar types", "[graph-safety]")
{
    AnimationGraphDescriptor descriptor;
    descriptor.BooleanLookUpTable = {0, 3};
    descriptor.FloatLookupTable = {1, 4};
    descriptor.IntegerLookupTable = {2, 5};
    std::array<uint32_t, 6> words{1, std::bit_cast<uint32_t>(2.f), 7, 99, 99, 99};
    std::array<hkbVariableInfo, 6> infos{};
    infos[0].type = hkbVariableInfo::kBool;
    infos[1].type = hkbVariableInfo::kReal;
    infos[2].type = hkbVariableInfo::kInt32;
    infos[3].type = hkbVariableInfo::kPointer;
    infos[4].type = hkbVariableInfo::kVector4;
    infos[5].type = hkbVariableInfo::kQuaternion;
    const auto cRead = VariableSync::Read(descriptor, std::span(words).first(3), infos);
    CHECK(cRead.Booleans == std::vector<bool>{true, false});
    CHECK(cRead.Floats == std::vector<float>{2.f, 0.f});
    CHECK(cRead.Integers == std::vector<uint32_t>{7, 0});

    AnimationVariables incoming;
    incoming.Booleans = {false, true};
    incoming.Floats = {4.f, 5.f};
    incoming.Integers = {8, 9};
    VariableSync::Write(descriptor, std::span(words).first(3), infos, incoming);
    CHECK(words == std::array<uint32_t, 6>{0, std::bit_cast<uint32_t>(4.f), 8, 99, 99, 99});

    WHEN("indices fit but their storage holds Havok indices or has another scalar type")
    {
        infos[0].type = hkbVariableInfo::kPointer;
        infos[1].type = hkbVariableInfo::kBool;
        infos[2].type = hkbVariableInfo::kVector3;
        const auto cBefore = words;
        VariableSync::Write(descriptor, words, infos, incoming);
        CHECK(words == cBefore);
    }
    WHEN("the variable metadata is missing")
    {
        const auto cBefore = words;
        VariableSync::Write(descriptor, words, {}, incoming);
        CHECK(words == cBefore);
    }
    CHECK_FALSE(VariableSync::NeedsDescriptor(42, 100, 100));
    CHECK(VariableSync::NeedsDescriptor(42, 100, 200));
    CHECK(VariableSync::NeedsDescriptor(0, 100, 100));
}

TEST_CASE("An acquired graph reference is released on every exit after its lock", "[graph-safety]")
{
    const bool cFound = GENERATE(false, true);
    const bool cWorkSucceeds = GENERATE(false, true);
    const bool cHasGraph = GENERATE(false, true);
    bool locked = false;
    int releases = 0;
    struct Graph
    {
        bool& Locked;
        int& Releases;
        void Release()
        {
            CHECK_FALSE(Locked);
            ++Releases;
        }
    } graph{locked, releases};
    const bool cResult = WithGraphReference<Graph>([&](Graph** appGraph) {
        *appGraph = cHasGraph ? &graph : nullptr;
        return cFound;
    }, [&](Graph*) {
        struct Lock
        {
            bool& Held;
            explicit Lock(bool& aHeld) : Held(aHeld) { Held = true; }
            ~Lock() { Held = false; }
        } lock(locked);
        return cWorkSucceeds;
    });
    CHECK(cResult == (cFound && cHasGraph && cWorkSucceeds));
    CHECK(releases == static_cast<int>(cHasGraph));
}
