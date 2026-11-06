#include <catch2/catch.hpp>

#include <Diagnostics/Counters.h>
#include <Games/Animation/GraphDrift.h>

#include <algorithm>
#include <cstring>

namespace
{
uint64_t CountOf(const char* acpName)
{
    const auto cAll = Diagnostics::Counters::Read();
    const auto it = std::ranges::find(cAll, std::string(acpName), &Diagnostics::Counters::Reading::Name);
    return it == cAll.end() ? 0 : it->Count;
}

void Put(std::string& aOut, const void* apData, size_t aSize)
{
    aOut.append(static_cast<const char*>(apData), aSize);
}

void PutString(std::string& aOut, std::string_view acText)
{
    const auto cLength = uint16_t(acText.size());
    Put(aOut, &cLength, 2);
    aOut.append(acText);
}

// TESObjectREFR::SaveAnimationGraphState's bytes: flags, then per graph its project and SaveState's output, here its
// active machines section and a tail standing for the other sections.
std::string Saved(const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>& acGraphs)
{
    std::string out(8, '\0');
    for (const auto& [cProject, cMachines] : acGraphs)
    {
        std::string graph;
        const auto cCount = uint32_t(cMachines.size());
        Put(graph, &cCount, 4);
        for (const auto& [cMachine, cState] : cMachines)
        {
            const uint32_t cWord = 7;
            PutString(graph, cMachine);
            Put(graph, &cWord, 4);
            PutString(graph, cState);
            Put(graph, &cWord, 4);
            Put(graph, &cWord, 4);
        }
        PutString(out, cProject);
        const auto cSize = uint32_t(graph.size());
        Put(out, &cSize, 4);
        out += graph;
    }
    return out;
}

const GraphDrift::States cOwner{"p/m=Gesture"};
const GraphDrift::States cElsewhere{"p/m=Idle"};
} // namespace

TEST_CASE("A saved graph's active states are read by name, every graph's, sorted", "[animation][graph-drift]")
{
    const auto cSaved = Saved({{"DefaultMale", {{"mt_behavior.hkb/Root", "NormalMovement"}, {"idlebehavior.hkb/Idle", "DialogueGeneric"}}},
                               {"Horse", {}}});
    const auto cStates = GraphDrift::ActiveStates(cSaved);
    REQUIRE(cStates);
    CHECK(*cStates == GraphDrift::States{"DefaultMale/idlebehavior.hkb/Idle=DialogueGeneric", "DefaultMale/mt_behavior.hkb/Root=NormalMovement"});
    CHECK_FALSE(GraphDrift::ActiveStates(cSaved.substr(0, cSaved.size() - 3)));
    CHECK(GraphDrift::ActiveStates(std::string(8, '\0'))->empty());
}

TEST_CASE("A copy landing where its owner did counts as matched", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cOwner, 1000);
    CHECK_FALSE(tracker.Open);
    CHECK(CountOf("graph drift replays") == 1);
    CHECK(CountOf("graph drift matched") == 1);
    CHECK(CountOf("graph drift mismatches") == 0);
}

TEST_CASE("Paired actions and actions with no snapshot are skipped, never compared", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    CHECK_FALSE(GraphDrift::Compares(true, false, true));
    CHECK_FALSE(GraphDrift::Compares(false, false, false));
    CHECK_FALSE(GraphDrift::Compares(false, true, true));
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "pa_KillMove", true, false, "", std::nullopt, std::nullopt, 1000);
    GraphDrift::Taken(tracker, 0x10, 0x2, "gesture", false, false, "", std::nullopt, std::nullopt, 1100);
    CHECK(CountOf("graph drift skipped") == 2);
    CHECK(CountOf("graph drift matched") == 0);
    // A snapshot that does not parse is not a skip.
    GraphDrift::Taken(tracker, 0x10, 0x3, "gesture", false, false, "snapshot", std::nullopt, cOwner, 1200);
    CHECK(CountOf("graph drift unreadable") == 1);
    // A player's copy: its owner's first-person graph is never the copy's.
    GraphDrift::Taken(tracker, 0x10, 0x4, "WeapEquip", false, true, "snapshot", cOwner, cElsewhere, 1300);
    CHECK(CountOf("graph drift players") == 1);
    CHECK_FALSE(tracker.Open);
}

TEST_CASE("A copy that catches up within the threshold counts as transient, with its duration", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cElsewhere, 1000);
    CHECK(tracker.Open);
    CHECK_FALSE(GraphDrift::SampleDue(tracker, 1050));
    REQUIRE(GraphDrift::SampleDue(tracker, 1100));
    (void)GraphDrift::Sample(tracker, 0x10, cElsewhere, 1100);
    (void)GraphDrift::Sample(tracker, 0x10, cOwner, 1250);
    CHECK_FALSE(tracker.Open);
    const auto cAll = Diagnostics::Counters::Read();
    const auto it = std::ranges::find(cAll, std::string("graph drift transient ms"), &Diagnostics::Counters::Reading::Name);
    REQUIRE(it != cAll.end());
    CHECK(it->Count == 1);
    CHECK(it->Max == 250);
    CHECK(CountOf("graph drift lasting") == 0);
}

TEST_CASE("A copy still apart past the threshold counts as lasting once", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cElsewhere, 1000);
    for (uint64_t now = 1100; now <= 1000 + GraphDrift::cLastingMs + 500; now += GraphDrift::cSampleMs)
    {
        const bool cLoad = GraphDrift::Sample(tracker, 0x10, cElsewhere, now);
        CHECK(cLoad == (now == 3000)); // once, as it turns lasting
    }
    CHECK(tracker.Lasting);
    CHECK(tracker.Snapshot == "snapshot");
    CHECK(CountOf("graph drift lasting") == 1);
    // The next replay ends it: its duration goes to the lasting ones.
    GraphDrift::Taken(tracker, 0x10, 0x2, "gesture", false, false, "snapshot", cOwner, cOwner, 4000);
    CHECK(CountOf("graph drift lasting ms") == 1);
    CHECK(CountOf("graph drift transient ms") == 0);
    CHECK(CountOf("graph drift matched") == 1);
}

TEST_CASE("A sync point moves an open mismatch to the owner's newer states", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cElsewhere, 1000);
    GraphDrift::Retarget(tracker, "newer", cElsewhere);
    CHECK(tracker.Snapshot == "newer");
    (void)GraphDrift::Sample(tracker, 0x10, cElsewhere, 1100);
    CHECK_FALSE(tracker.Open);
    CHECK(CountOf("graph drift transient ms") == 1);
}

TEST_CASE("A copy whose graph cannot be read during an open mismatch is dropped, never counted as lasting", "[animation][graph-drift]")
{
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cElsewhere, 1000);
    for (uint64_t now = 1100; now <= 3100; now += GraphDrift::cSampleMs)
        (void)GraphDrift::Sample(tracker, 0x10, std::nullopt, now);
    CHECK_FALSE(tracker.Open);
    CHECK(CountOf("graph drift unreadable") == 1);
    CHECK(CountOf("graph drift lasting") == 0);
    CHECK(CountOf("graph drift transient ms") == 0);
}

TEST_CASE("A lasting mismatch's load is counted, and the copy is read again at once", "[animation][graph-drift]")
{
    const bool cLoaded = GENERATE(false, true);
    Diagnostics::Counters::Reset();
    GraphDrift::Tracker tracker;
    GraphDrift::Taken(tracker, 0x10, 0x1, "gesture", false, false, "snapshot", cOwner, cElsewhere, 1000);
    REQUIRE(GraphDrift::Sample(tracker, 0x10, cElsewhere, 1000 + GraphDrift::cLastingMs));
    GraphDrift::Loaded(tracker, 0x10, cLoaded);
    CHECK(CountOf("graph drift loads") == (cLoaded ? 1 : 0));
    CHECK(CountOf("graph drift load failures") == (cLoaded ? 0 : 1));
    CHECK(GraphDrift::SampleDue(tracker, 1000 + GraphDrift::cLastingMs + 1) == cLoaded);
    if (cLoaded)
    {
        CHECK_FALSE(GraphDrift::Sample(tracker, 0x10, cOwner, 1000 + GraphDrift::cLastingMs + 16));
        CHECK_FALSE(tracker.Open);
        CHECK(CountOf("graph drift lasting ms") == 1);
    }
    // Never loaded twice for one mismatch.
    CHECK_FALSE(GraphDrift::Sample(tracker, 0x10, cElsewhere, 1000 + 2 * GraphDrift::cLastingMs));
}
