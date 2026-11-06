#include <Games/Animation/GraphDrift.h>

#include <Diagnostics/Counters.h>

#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace GraphDrift
{
namespace
{
struct Reader
{
    std::string_view Data;
    size_t Offset = 0;
    bool Bad = false;

    template <class T> T Take() noexcept
    {
        T value{};
        if (Offset + sizeof(T) > Data.size())
        {
            Bad = true;
            return value;
        }
        std::memcpy(&value, Data.data() + Offset, sizeof(T));
        Offset += sizeof(T);
        return value;
    }
    std::string_view Bytes(size_t aSize) noexcept
    {
        if (Offset + aSize > Data.size())
        {
            Bad = true;
            return {};
        }
        const auto cOut = Data.substr(Offset, aSize);
        Offset += aSize;
        return cOut;
    }
    std::string_view String() noexcept { return Bytes(Take<uint16_t>()); }
};

std::string Join(const States& acStates)
{
    std::string out;
    for (const auto& cState : acStates)
        out += (out.empty() ? "" : ", ") + cState;
    return out.empty() ? "(none)" : out;
}

void End(Tracker& aTracker, uint32_t aFormId, uint64_t aNow, const char* acpHow) noexcept
{
    const uint64_t cMs = aNow - aTracker.Since;
    if (aTracker.Lasting)
    {
        COUNT("graph drift lasting ms", cMs);
        spdlog::info("Graph drift: copy {:X} after {:X} '{}' ended after {} ms ({})", aFormId, aTracker.ActionId, aTracker.Event, cMs, acpHow);
    }
    else
    {
        COUNT("graph drift transient ms", cMs);
        spdlog::debug("Graph drift: copy {:X} after {:X} '{}' transient, {} ms ({})", aFormId, aTracker.ActionId, aTracker.Event, cMs, acpHow);
    }
    aTracker = Tracker{};
}
} // namespace

std::optional<States> ActiveStates(std::string_view acSaved) noexcept
{
    // 8 bytes of ActorState flags, then per graph: project (u16 length), state (u32 length, SaveState's bytes).
    Reader actor{acSaved};
    actor.Take<uint64_t>();
    States states;
    while (!actor.Bad && actor.Offset < acSaved.size())
    {
        const auto cProject = actor.String();
        Reader graph{actor.Bytes(actor.Take<uint32_t>())};
        if (actor.Bad)
            return std::nullopt;
        // Active machines: machine, u32, current state, u32, u32 (SaveState 63601's first section).
        const uint32_t cCount = graph.Take<uint32_t>();
        for (uint32_t i = 0; i < cCount && !graph.Bad; ++i)
        {
            const auto cMachine = graph.String();
            graph.Take<uint32_t>();
            const auto cState = graph.String();
            graph.Take<uint32_t>();
            graph.Take<uint32_t>();
            states.push_back(std::string(cProject) + "/" + std::string(cMachine) + "=" + std::string(cState));
        }
        if (graph.Bad)
            return std::nullopt;
    }
    if (actor.Bad)
        return std::nullopt;
    std::ranges::sort(states);
    return states;
}

void Taken(Tracker& aTracker, uint32_t aFormId, uint32_t aActionId, std::string_view acEvent, bool aPaired, bool aPlayer, bool aHasSnapshot,
           const std::optional<States>& acExpected, const std::optional<States>& acActual, uint64_t aNow) noexcept
{
    COUNT("graph drift replays", 1);
    if (aTracker.Open)
        End(aTracker, aFormId, aNow, "superseded");
    // A paired action carries no snapshot (its partner's half is its own).
    if (aPlayer)
    {
        COUNT("graph drift players", 1);
        return;
    }
    if (!Compares(aPaired, aPlayer, aHasSnapshot))
    {
        COUNT("graph drift skipped", 1);
        return;
    }
    if (!acExpected || !acActual)
    {
        COUNT("graph drift unreadable", 1);
        return;
    }
    if (*acExpected == *acActual)
    {
        COUNT("graph drift matched", 1);
        return;
    }
    COUNT("graph drift mismatches", 1);
    aTracker = Tracker{*acExpected, *acActual, aNow, aNow, aActionId, std::string(acEvent), true, false};
    spdlog::debug("Graph drift: copy {:X} after {:X} '{}': owner [{}], copy [{}]", aFormId, aActionId, acEvent, Join(*acExpected),
                  Join(*acActual));
}

bool SampleDue(const Tracker& acTracker, uint64_t aNow) noexcept
{
    return acTracker.Open && aNow - acTracker.LastSample >= cSampleMs;
}

void Sample(Tracker& aTracker, uint32_t aFormId, const std::optional<States>& acActual, uint64_t aNow) noexcept
{
    if (!aTracker.Open)
        return;
    aTracker.LastSample = aNow;
    // No reading is no evidence: the window ends uncounted, never stretched into a lasting one.
    if (!acActual)
    {
        COUNT("graph drift unreadable", 1);
        spdlog::debug("Graph drift: copy {:X} after {:X} '{}': graph unreadable after {} ms, dropped", aFormId, aTracker.ActionId,
                      aTracker.Event, aNow - aTracker.Since);
        aTracker = Tracker{};
        return;
    }
    if (*acActual == aTracker.Expected)
    {
        End(aTracker, aFormId, aNow, "matched");
        return;
    }
    if (!aTracker.Lasting && aNow - aTracker.Since >= cLastingMs)
    {
        aTracker.Lasting = true;
        COUNT("graph drift lasting", 1);
        spdlog::warn("Graph drift: copy {:X} after {:X} '{}' still apart after {} ms: owner [{}], copy [{}]", aFormId, aTracker.ActionId,
                     aTracker.Event, aNow - aTracker.Since, Join(aTracker.Expected), Join(*acActual));
    }
}

void Retarget(Tracker& aTracker, const std::optional<States>& acExpected) noexcept
{
    if (aTracker.Open && acExpected)
        aTracker.Expected = *acExpected;
}
} // namespace GraphDrift
