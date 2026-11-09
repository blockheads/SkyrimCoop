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
    uint8_t ClipSet = 0; // with Event: not sent, a clip load: its owner's process loaded the event's clip files into
                         // that set (ActionClips::Set), which the copy's loads before its next ops
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

struct GraphOps
{
    std::vector<GraphOpFrame> Frames;
    std::vector<GraphOpsRun> Runs;
    bool operator==(const GraphOps&) const noexcept = default;

    // An update in the frame after its run's last one that changes nothing is one bit; otherwise a few bits of kind,
    // then each variable's index step and the non-zero bytes of its XOR with its last value in the run (each batch
    // decodes alone: the host relays it to every guest).
    void Serialize(Wire::Buffer::Writer& aWriter) const;
    [[nodiscard]] bool Deserialize(Wire::Buffer::Reader& aReader);

    static void WriteRun(Wire::Buffer::Writer& aWriter, const GraphOpsRun& acRun);
    [[nodiscard]] static bool ReadRun(Wire::Buffer::Reader& aReader, GraphOpsRun& aRun, size_t aFrames);
    static void WriteFrames(Wire::Buffer::Writer& aWriter, const std::vector<GraphOpFrame>& acFrames);
    [[nodiscard]] static bool ReadFrames(Wire::Buffer::Reader& aReader, std::vector<GraphOpFrame>& aFrames);
};
