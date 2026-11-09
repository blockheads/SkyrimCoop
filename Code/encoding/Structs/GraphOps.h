#pragma once

#include <Wire/Buffer.h>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// An owner's graph inputs, in order, for its copies to replay (docs/design/graph-input-stream.md). Values are exact:
// a graph decides on the bits it reads.
struct GraphSeed
{
    uint32_t Seed = 0;
    uint32_t Current = 0;
    bool operator==(const GraphSeed&) const noexcept = default;
};

struct GraphOp
{
    bool Event = false;
    uint32_t Frame = 0;                                 // an update's frame: its index in GraphOps::Frames
    uint16_t EventId = 0;                               // an event's index in the graph's event table
    std::optional<GraphSeed> Seed;                      // the graph's generator, when it changed since the last op
    std::vector<std::pair<uint16_t, uint32_t>> Changed; // variables set since the last op ended, by index, ascending
    bool operator==(const GraphOp&) const noexcept = default;
};

// One actor's ops in a batch.
struct GraphOpsRun
{
    uint32_t ActorId = 0;     // its server id
    uint32_t FirstUpdate = 0; // the index of its first update op, counting from its stream's start
    std::vector<GraphOp> Ops;
    bool operator==(const GraphOpsRun&) const noexcept = default;
};

struct GraphOpFrame
{
    uint64_t Tick = 0; // the owner's frame tick
    float Step = 0.f;  // its graphs' step (63587's deltaTime): one per frame for every actor
    bool operator==(const GraphOpFrame&) const noexcept = default;
};

// What each side remembers of an actor's stream between batches, so a value goes as its XOR with the last one sent.
// Reliable, in-order delivery keeps both alike; a gap resets both (a sync point).
struct GraphOpsHistory
{
    std::vector<uint32_t> Values;
};

struct GraphOps
{
    std::vector<GraphOpFrame> Frames;
    std::vector<GraphOpsRun> Runs;
    bool operator==(const GraphOps&) const noexcept = default;

    // An update that changes nothing in the frame after the last one is one bit; otherwise a few bits of kind, then
    // each variable's index step and the non-zero bytes of its XOR with its last value.
    template <class THistories> void Serialize(Wire::Buffer::Writer& aWriter, THistories& aHistories) const;
    template <class THistories> [[nodiscard]] bool Deserialize(Wire::Buffer::Reader& aReader, THistories& aHistories);

    static void WriteRun(Wire::Buffer::Writer& aWriter, const GraphOpsRun& acRun, GraphOpsHistory& aHistory);
    [[nodiscard]] static bool ReadRun(Wire::Buffer::Reader& aReader, GraphOpsRun& aRun, GraphOpsHistory& aHistory,
                                      size_t aFrames);
    static void WriteFrames(Wire::Buffer::Writer& aWriter, const std::vector<GraphOpFrame>& acFrames);
    [[nodiscard]] static bool ReadFrames(Wire::Buffer::Reader& aReader, std::vector<GraphOpFrame>& aFrames);
};

// THistories: a map from ActorId to GraphOpsHistory (operator[]).
template <class THistories> void GraphOps::Serialize(Wire::Buffer::Writer& aWriter, THistories& aHistories) const
{
    WriteFrames(aWriter, Frames);
    aWriter.WriteBits(Runs.size(), 16);
    for (const auto& cRun : Runs)
        WriteRun(aWriter, cRun, aHistories[cRun.ActorId]);
}

template <class THistories> bool GraphOps::Deserialize(Wire::Buffer::Reader& aReader, THistories& aHistories)
{
    if (!ReadFrames(aReader, Frames))
        return false;
    uint64_t runs = 0;
    if (!aReader.ReadBits(runs, 16))
        return false;
    Runs.assign(runs, {});
    for (auto& run : Runs)
    {
        uint64_t id = 0;
        if (!aReader.ReadBits(id, 32))
            return false;
        run.ActorId = static_cast<uint32_t>(id);
        if (!ReadRun(aReader, run, aHistories[run.ActorId], Frames.size()))
            return false;
    }
    return true;
}
