#include <animation/GraphStream.h>
#include <animation/PoseBlend.h>

#include <catch2/catch.hpp>

#include <cmath>
#include <vector>

namespace
{
std::vector<PoseBone> At(float aX)
{
    std::vector<PoseBone> pose(1);
    pose[0].Translation[0] = aX;
    return pose;
}
} // namespace

TEST_CASE("A blended bone lerps its place and scale and turns the short way", "[pose-blend]")
{
    std::vector<PoseBone> from(1), to(1), out;
    from[0].Translation[0] = 0.f;
    to[0].Translation[0] = 10.f;
    to[0].Scale[0] = 3.f;
    // 90 degrees about z, given as its negative quaternion: the same rotation, the long way round as written.
    const float cHalf = std::sqrt(0.5f);
    to[0].Rotation[2] = -cHalf;
    to[0].Rotation[3] = -cHalf;
    PoseTimeline::Blend(from, to, 0.5f, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].Translation[0] == Approx(5.f));
    CHECK(out[0].Scale[0] == Approx(2.f));
    // Halfway is 45 degrees: w = cos(22.5), z = sin(22.5) with the sign taken to the short way.
    CHECK(std::abs(out[0].Rotation[3]) == Approx(std::cos(0.3926991f)).margin(1e-4));
    CHECK(std::abs(out[0].Rotation[2]) == Approx(std::sin(0.3926991f)).margin(1e-4));
    CHECK(out[0].Rotation[2] * out[0].Rotation[3] > 0.f);
}

TEST_CASE("A blend over poses of different LODs takes the newer one's extra bones as they are", "[pose-blend]")
{
    std::vector<PoseBone> from = At(0.f), to(2), out;
    to[0].Translation[0] = 4.f;
    to[1].Translation[0] = 7.f;
    PoseTimeline::Blend(from, to, 0.5f, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].Translation[0] == Approx(2.f));
    CHECK(out[1].Translation[0] == Approx(7.f));
}

TEST_CASE("A copy's pose is shown one and a half of its owner's updates behind playback, between the two around it", "[pose-blend]")
{
    PoseTimeline poses;
    std::vector<PoseBone> out;
    CHECK_FALSE(poses.Shown(100, out)); // nothing to blend yet
    for (uint64_t tick = 20; tick <= 20 * 200; tick += 20) // its spacing settles on 20 ms
        poses.Add(tick, At(static_cast<float>(tick)));
    REQUIRE(poses.Shown(4020, out));
    CHECK(out[0].Translation[0] == Approx(3990.f).margin(0.1));
    REQUIRE(poses.Shown(9000, out)); // its owner's next is late: the newest held, never past it
    CHECK(out[0].Translation[0] == Approx(4000.f));
    REQUIRE(poses.Shown(0, out)); // never before the oldest kept
    CHECK(out[0].Translation[0] == Approx(3900.f));
}

TEST_CASE("A copy whose owner's clock goes back shows its new poses, not the old ones until it catches up", "[pose-blend]")
{
    PoseTimeline poses;
    std::vector<PoseBone> out;
    for (const uint64_t cTick : {1000, 1016, 700, 716, 732})
        poses.Add(cTick, At(static_cast<float>(cTick)));
    REQUIRE(poses.Shown(741, out));
    CHECK(out[0].Translation[0] == Approx(716.f).margin(1.0));
}

TEST_CASE("A copy's shown pose moves on with its frames whether its owner's updates come 0, 1 or 2 a frame", "[pose-blend]")
{
    // Skyrim: owners step once a frame; a copy's graph ran 0 or 2 of their updates in 5-23 % of its frames.
    PoseTimeline poses;
    std::vector<PoseBone> out;
    uint64_t owner = 0;
    float last = -1.f;
    const int cRan[] = {1, 1, 0, 2, 1, 0, 2, 1, 1, 1, 2, 0};
    for (int frame = 0; frame < 240; ++frame)
    {
        for (int i = 0; i < cRan[frame % 12]; ++i)
        {
            owner += 20;
            poses.Add(owner, At(static_cast<float>(owner)));
        }
        const uint64_t cPlayback = 20 * static_cast<uint64_t>(frame + 1) + 5;
        if (!poses.Shown(cPlayback, out))
            continue;
        if (frame > 150) // its spacing has settled
            CHECK(out[0].Translation[0] - last == Approx(20.f).margin(0.1));
        last = out[0].Translation[0];
    }
}

TEST_CASE("A copy's replay keeps each generate pass's pose and shows the blend at its playback", "[pose-blend][graph-stream]")
{
    struct PosedGraph final : IGraphTarget
    {
        std::vector<PoseBone> Pose;
        float Time = 0.f;
        std::vector<uint32_t> Words() override { return {0, 0, 0, 0}; }
        void WriteWords(std::span<const uint32_t>) override {}
        void SetSeed(const GraphSeed&) override {}
        bool LoadPoint(const Payloads::GraphSyncPoint&) override { return true; }
        void SendEvent(uint16_t) override {}
        std::shared_ptr<std::atomic<bool>> RequestClips(uint8_t, uint16_t) override { return nullptr; }
        void Picks(std::span<const GraphPick>) override {}
        void Update(float aStep) override { Time += aStep * 1000.f; }
        void Generate() override { Pose = At(Time); }
        void HoldAnimObjects(std::span<const std::pair<uint32_t, bool>>) override {}
        std::span<const PoseBone> GeneratedPose() override { return Pose; }
        void ShowPose(std::span<const PoseBone> aPose) override { Pose.assign(aPose.begin(), aPose.end()); }
    };
    GraphRecorder recorder;
    std::vector<uint32_t> words(4, 0);
    for (uint64_t tick = 20; tick <= 100; tick += 20)
    {
        recorder.Update(tick, 0.02f, words, {});
        recorder.Updated(words);
        recorder.Generate(tick);
    }
    GraphRecordedRun run{1, 0, {}};
    run.Ops = recorder.Take(run.FirstUpdate);
    const GraphOps cOps = BatchGraphOps({run});
    GraphReplayer replayer;
    Payloads::GraphSyncPoint point;
    point.State = "0";
    replayer.Load(point);
    replayer.Feed(cOps.Runs[0], cOps.Frames);
    PosedGraph copy;
    CHECK(replayer.Run(100, copy) == 4); // behind: 4 a frame, each pose kept
    REQUIRE(copy.Pose.size() == 1);
    const float cShown = copy.Pose[0].Translation[0];
    CHECK(cShown > 60.f); // between updates 60 and 80, not the newest generated
    CHECK(cShown < 80.f);
    CHECK(replayer.Run(100, copy) == 1);
    CHECK(replayer.Run(105, copy) == 0); // no update this frame: its pose moves on all the same
    CHECK(copy.Pose[0].Translation[0] == Approx(cShown + 5.f).margin(1.0));
}
