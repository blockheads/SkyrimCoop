// The copies' playback time (CopyClock, CopyPlayback; docs/design/copy-clock.md): as little delay as the line
// allows, steered, never back.
#include <catch2/catch.hpp>

#include <Services/CopyClock.h>
#include <Systems/CopyCorrection.h>
#include <Systems/CopyLead.h>

#include <algorithm>
#include <deque>

namespace
{
constexpr uint64_t cFrameMs = 16;
constexpr uint64_t cStart = 100000;

// Snapshots are seen at the next frame: a line's lateness plus up to a frame, and the margin.
bool DelayFor(uint64_t aNetwork, uint64_t aLatencyMs)
{
    return aNetwork >= aLatencyMs + CopyClock::kMarginMs && aNetwork <= aLatencyMs + cFrameMs + CopyClock::kMarginMs;
}

// One owner sending one copy's snapshots over a line, played on this game's clock each frame.
struct Line
{
    uint64_t IntervalMs = 33;
    uint64_t LatencyMs = 20;
    uint64_t JitterMs = 0;
    uint64_t SpikeAt = 0; // the snapshot sent at this tick arrives SpikeMs late
    uint64_t SpikeMs = 0;
    uint64_t ReadJitterMs = 0; // the session's time read up to this stale (where the network last updated it)

    CopyClock Clock;
    CopyPlayback Copy;
    uint64_t Now = cStart;
    uint64_t Newest = 0; // the newest snapshot here
    uint64_t Underruns = 0;
    uint64_t Backwards = 0;

    // The tick of the snapshot sent after aSkip more.
    uint64_t SendAfter(uint64_t aSkip) const { return m_nextSend + aSkip * IntervalMs; }

    void Run(uint64_t aMs, uint64_t aCountAfter = 0)
    {
        const uint64_t cEnd = Now + aMs;
        uint64_t previous = 0;
        for (; Now < cEnd; Now += cFrameMs)
        {
            while (m_nextSend <= Now)
            {
                uint64_t late = LatencyMs + (JitterMs ? Random() % (JitterMs + 1) : 0);
                if (m_nextSend == SpikeAt)
                    late = SpikeMs;
                m_flight.push_back({m_nextSend, m_nextSend + late});
                m_nextSend += IntervalMs;
            }
            std::ranges::sort(m_flight, {}, &Flight::Arrives);
            while (!m_flight.empty() && m_flight.front().Arrives <= Now)
            {
                Clock.OnSnapshot(m_flight.front().Sent, Now);
                Copy.OnPoint(m_flight.front().Sent);
                Newest = std::max(Newest, m_flight.front().Sent);
                m_flight.pop_front();
            }
            Clock.Advance(Now - (ReadJitterMs ? Random() % (ReadJitterMs + 1) : 0), cFrameMs);
            const uint64_t cTick = Copy.At(Clock);
            if (Now >= aCountAfter)
            {
                Underruns += cTick > Newest;
                Backwards += cTick < previous;
            }
            previous = cTick;
        }
    }

private:
    struct Flight
    {
        uint64_t Sent;
        uint64_t Arrives;
    };
    uint64_t Random() { return m_seed = m_seed * 6364136223846793005ull + 1442695040888963407ull, m_seed >> 33; }

    std::deque<Flight> m_flight;
    uint64_t m_nextSend = cStart;
    uint64_t m_seed = 7;
};
} // namespace

TEST_CASE("A good line plays copies a little behind, not a fixed 300 ms", "[copy-clock]")
{
    Line line;
    line.Run(5000, cStart + 500);
    CHECK(DelayFor(line.Clock.Network(), 20));
    CHECK(line.Clock.Delay() == Approx(line.Clock.Network()).margin(cFrameMs));
    CHECK(line.Copy.Interval() == Approx(33).margin(1));
    CHECK(line.Now - line.Copy.At(line.Clock) < 100);
    CHECK(line.Underruns == 0);
}

TEST_CASE("A jittery line still has the next snapshot when the copy reaches it", "[copy-clock]")
{
    const uint64_t cInterval = GENERATE(33, 100);
    CAPTURE(cInterval);
    Line line;
    line.IntervalMs = cInterval;
    line.LatencyMs = 40;
    line.JitterMs = 80;
    line.Run(20000, cStart + 3000);
    CHECK(line.Underruns == 0);
    CHECK(line.Backwards == 0);
    CHECK(line.Clock.Network() <= 40 + 80 + cFrameMs + CopyClock::kMarginMs);
}

TEST_CASE("A late snapshot raises the delay at once; it falls only when the window forgets it", "[copy-clock]")
{
    Line line;
    line.Run(3000);
    line.SpikeAt = line.SendAfter(2);
    line.SpikeMs = 220;
    line.Run(500);
    CHECK(DelayFor(line.Clock.Network(), 220));

    SECTION("the clock slows toward it, never faster than kSlowest allows")
    {
        uint64_t previous = line.Clock.Now();
        for (int i = 0; i < 20; ++i)
        {
            line.Run(cFrameMs);
            const uint64_t cStep = line.Clock.Now() - previous;
            CHECK(cStep >= static_cast<uint64_t>(cFrameMs * CopyClock::kSlowest) - 1);
            CHECK(cStep <= static_cast<uint64_t>(cFrameMs * CopyClock::kFastest) + 1);
            previous = line.Clock.Now();
        }
    }
    SECTION("forgotten after the window")
    {
        line.Run(CopyClock::kWindowMs);
        CHECK(DelayFor(line.Clock.Network(), 20));
        CHECK(line.Backwards == 0);
    }
}

TEST_CASE("A game that stalled jumps ahead, never back", "[copy-clock]")
{
    CopyClock clock;
    clock.OnSnapshot(cStart - 20, cStart);
    clock.Advance(cStart, 0);
    const uint64_t cBefore = clock.Now();
    clock.Advance(cStart + 2000, 2000);
    CHECK(clock.Now() == cStart + 2000 - clock.Network());
    CHECK(clock.Now() > cBefore);

    SECTION("the session's time going back holds it")
    {
        const uint64_t cHeld = clock.Now();
        clock.Advance(cStart + 1000, cFrameMs);
        CHECK(clock.Now() == cHeld);
    }
}

TEST_CASE("The delay stays bounded however late the line is", "[copy-clock]")
{
    CopyClock clock;
    clock.OnSnapshot(cStart, cStart + 5000);
    clock.Advance(cStart + 5000, 0);
    CHECK(clock.Network() == CopyClock::kMaxMs);
    CHECK(clock.Delay() == CopyClock::kMaxMs);
}

TEST_CASE("A copy whose owner sends less often is steered further behind, smoothly", "[copy-clock]")
{
    Line line; // in a fight: 30/s
    line.Run(3000);
    REQUIRE(line.Copy.Interval() == Approx(33).margin(1));
    line.IntervalMs = 100; // out of it: 10/s
    double previous = line.Copy.Interval();
    for (int i = 0; i < 300; ++i)
    {
        line.Run(cFrameMs);
        CHECK(std::abs(line.Copy.Interval() - previous) <= CopyPlayback::kSteer * cFrameMs + 0.01);
        previous = line.Copy.Interval();
    }
    CHECK(line.Copy.Interval() == Approx(100).margin(1));
    CHECK(line.Backwards == 0);
}

TEST_CASE("A teleport puts the copy at its time at once, even back", "[copy-clock]")
{
    Line line;
    line.Run(2000);
    line.IntervalMs = 100;
    line.Run(200);
    const uint64_t cBefore = line.Copy.At(line.Clock);
    line.Copy.Snap(line.Clock);
    CHECK(line.Copy.At(line.Clock) == Approx(line.Clock.Now() - line.Copy.TargetInterval()).margin(1));
    CHECK(line.Copy.At(line.Clock) < cBefore);
}

TEST_CASE("Every service asking in one frame gets one tick", "[copy-clock]")
{
    Line line;
    line.Run(1000);
    const uint64_t cFirst = line.Copy.At(line.Clock);
    CHECK(line.Copy.At(line.Clock) == cFirst);
    line.Clock.Advance(line.Now + cFrameMs, cFrameMs);
    CHECK(line.Copy.At(line.Clock) > cFirst);
}

TEST_CASE("Steady frames step the copies steadily however unevenly the session's time is read", "[copy-clock]")
{
    // S10: the session's time is where the network last updated it, read 0-12 ms late each frame; on a fast line
    // the delay is about that much.
    Line line;
    line.LatencyMs = 1;
    line.ReadJitterMs = 12;
    line.Run(2000);
    for (int frame = 0; frame < 120; ++frame)
    {
        const uint64_t cBefore = line.Copy.At(line.Clock);
        line.Run(cFrameMs);
        const uint64_t cStep = line.Copy.At(line.Clock) - cBefore;
        CHECK(cStep >= static_cast<uint64_t>(cFrameMs * CopyClock::kSlowest) - 1);
        CHECK(cStep <= static_cast<uint64_t>(cFrameMs * CopyClock::kFastest) + 2);
    }
}

TEST_CASE("The frame clock steps with the frames and follows the session's time read unevenly", "[copy-clock]")
{
    // S10: owners stamp snapshots with it; the session's time is read 0-12 ms stale each frame.
    FrameClock clock;
    uint64_t seed = 7;
    const auto cStale = [&] { return (seed = seed * 6364136223846793005ull + 1442695040888963407ull, (seed >> 33) % 13); };
    uint64_t session = cStart;
    clock.Advance(session, 0);
    for (int frame = 0; frame < 300; ++frame)
    {
        const uint64_t cBefore = clock.Now();
        session += cFrameMs;
        clock.Advance(session - cStale(), cFrameMs);
        if (frame > 60)
        {
            CHECK(clock.Now() - cBefore >= cFrameMs - 2);
            CHECK(clock.Now() - cBefore <= cFrameMs + 2);
            CHECK(session - clock.Now() <= 12);
        }
    }

    SECTION("a load's jump is taken at once")
    {
        clock.Advance(session + 5000, cFrameMs);
        CHECK(clock.Now() == session + 5000);
    }
}

TEST_CASE("A late snapshot's correction is blended out, never jumped", "[copy-clock][copy-correction]")
{
    CopyCorrection correction;
    correction.Begin({12.f, 0.f, 0.f}, 1000);
    CHECK(correction.At(1000).x == Approx(12.f));
    CHECK(correction.At(1050).x == Approx(6.f));
    CHECK(correction.At(1000 + CopyCorrection::kMs).x == 0.f);
    CHECK(correction.At(5000).x == 0.f);

    SECTION("a teleport-sized one jumps")
    {
        correction.Begin({CopyCorrection::kMax + 1.f, 0.f, 0.f}, 2000);
        CHECK(correction.At(2000).x == 0.f);
    }
    SECTION("none until begun")
    {
        CHECK(CopyCorrection{}.At(123).x == 0.f);
    }
}

TEST_CASE("A copy's predicted place looks ahead by its delay and the line's", "[copy-clock][combat-aim]")
{
    // Its owner is aBehind ahead of it now, and moves on while a hit travels back over the line (combat-aim.md).
    CHECK(CopyLead::Ms(80, 60) == 140);
    CHECK(CopyLead::Ms(0, 0) == 0);
    // A stalled line guesses no further than half a second.
    CHECK(CopyLead::Ms(900, 400) == CopyLead::kMaxMs);
}

TEST_CASE("A copy's line is its own snapshots' lateness, a spike smoothed", "[copy-clock][combat-aim]")
{
    CopyLine line;
    for (uint64_t tick = 1000; tick < 3000; tick += 33)
        line.OnSnapshot(tick, tick + 60);
    CHECK(line.Ms() == 60);
    line.OnSnapshot(3000, 3300); // one late packet
    CHECK(line.Ms() == 84);
    for (uint64_t tick = 3033; tick < 4000; tick += 33)
        line.OnSnapshot(tick, tick + 60);
    CHECK(line.Ms() <= 61); // forgotten within a second
    // A clock ahead of ours reads as no delay, never wraps.
    line.OnSnapshot(5000, 4990);
    CHECK(line.Ms() < 60);
}
