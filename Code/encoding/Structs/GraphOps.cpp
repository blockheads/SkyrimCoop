#include <Structs/GraphOps.h>
#include <Wire/Serialization.h>
#include <bit>

namespace
{
constexpr uint64_t cMaxFrames = 4096;
constexpr uint64_t cMaxRuns = 4096;
constexpr uint64_t cMaxOps = 65536;
constexpr uint64_t cMaxChanged = 4096;
constexpr uint32_t cNoFrame = UINT32_MAX;

bool ReadVarInt(Wire::Buffer::Reader& aReader, uint64_t& aValue) noexcept
{
    return Wire::Serialization::TryReadVarInt(aReader, aValue);
}
} // namespace

void GraphOps::WriteFrames(Wire::Buffer::Writer& aWriter, const std::vector<GraphOpFrame>& acFrames)
{
    Wire::Serialization::WriteVarInt(aWriter, acFrames.size());
    uint64_t tick = 0;
    for (const auto& cFrame : acFrames)
    {
        Wire::Serialization::WriteVarInt(aWriter, cFrame.Tick - tick); // ticks count up
        tick = cFrame.Tick;
        aWriter.WriteBits(std::bit_cast<uint32_t>(cFrame.Step), 32);
    }
}

bool GraphOps::ReadFrames(Wire::Buffer::Reader& aReader, std::vector<GraphOpFrame>& aFrames)
{
    uint64_t count = 0;
    if (!ReadVarInt(aReader, count) || count > cMaxFrames)
        return false;
    aFrames.assign(count, {});
    uint64_t tick = 0;
    for (auto& frame : aFrames)
    {
        uint64_t delta = 0, step = 0;
        if (!ReadVarInt(aReader, delta) || !aReader.ReadBits(step, 32))
            return false;
        tick += delta;
        frame.Tick = tick;
        frame.Step = std::bit_cast<float>(static_cast<uint32_t>(step));
    }
    return true;
}

void GraphOps::Serialize(Wire::Buffer::Writer& aWriter) const
{
    WriteFrames(aWriter, Frames);
    Wire::Serialization::WriteVarInt(aWriter, Runs.size());
    for (const auto& cRun : Runs)
        WriteRun(aWriter, cRun);
}

bool GraphOps::Deserialize(Wire::Buffer::Reader& aReader)
{
    uint64_t runs = 0;
    if (!ReadFrames(aReader, Frames) || !ReadVarInt(aReader, runs) || runs > cMaxRuns)
        return false;
    Runs.assign(runs, {});
    for (auto& run : Runs)
        if (!ReadRun(aReader, run, Frames.size()))
            return false;
    return true;
}

void GraphOps::WriteRun(Wire::Buffer::Writer& aWriter, const GraphOpsRun& acRun)
{
    std::vector<uint32_t> last; // each variable's last value in the run
    Wire::Serialization::WriteVarInt(aWriter, acRun.ActorId);
    Wire::Serialization::WriteVarInt(aWriter, acRun.FirstUpdate);
    Wire::Serialization::WriteVarInt(aWriter, acRun.Ops.size());
    uint32_t lastFrame = cNoFrame;
    for (size_t i = 0; i < acRun.Ops.size(); ++i)
    {
        const auto& cOp = acRun.Ops[i];
        const bool cUpdate = !cOp.Event && !cOp.Generate;
        // An update's generate pass right after it goes with it.
        const bool cGenerated = cUpdate && i + 1 < acRun.Ops.size() && acRun.Ops[i + 1].Generate;
        const bool cNext = cUpdate && lastFrame != cNoFrame && cOp.Frame == lastFrame + 1;
        const bool cEmpty = cNext && cGenerated && !cOp.Seed && cOp.Changed.empty();
        aWriter.WriteBits(cEmpty, 1);
        if (cUpdate)
            lastFrame = cOp.Frame;
        if (cGenerated)
            ++i;
        if (cEmpty)
            continue;
        aWriter.WriteBits(!cUpdate, 1);
        if (!cUpdate)
        {
            aWriter.WriteBits(cOp.Generate, 1);
            if (cOp.Generate)
                continue; // a generate pass after something else: nothing more
            aWriter.WriteBits(cOp.EventId, 16);
            aWriter.WriteBits(cOp.ClipSet != 0, 1);
            if (cOp.ClipSet)
                aWriter.WriteBits(cOp.ClipSet, 8);
        }
        else
        {
            aWriter.WriteBits(cNext, 1);
            if (!cNext)
                Wire::Serialization::WriteVarInt(aWriter, cOp.Frame);
            aWriter.WriteBits(cGenerated, 1);
        }
        aWriter.WriteBits(cOp.Seed.has_value(), 1);
        if (cOp.Seed)
        {
            aWriter.WriteBits(cOp.Seed->Seed, 32);
            aWriter.WriteBits(cOp.Seed->Current, 32);
        }
        Wire::Serialization::WriteVarInt(aWriter, cOp.Changed.size());
        uint32_t index = 0;
        for (const auto& [cIndex, cValue] : cOp.Changed)
        {
            Wire::Serialization::WriteVarInt(aWriter, cIndex - index); // ascending
            index = cIndex;
            if (last.size() <= cIndex)
                last.resize(cIndex + 1);
            const uint32_t cXor = cValue ^ last[cIndex];
            last[cIndex] = cValue;
            uint64_t mask = 0;
            for (int b = 0; b < 4; ++b)
                mask |= ((cXor >> (8 * b)) & 0xFF) ? (1u << b) : 0;
            aWriter.WriteBits(mask, 4);
            for (int b = 0; b < 4; ++b)
                if (mask & (1u << b))
                    aWriter.WriteBits((cXor >> (8 * b)) & 0xFF, 8);
        }
    }
}

bool GraphOps::ReadRun(Wire::Buffer::Reader& aReader, GraphOpsRun& aRun, size_t aFrames)
{
    std::vector<uint32_t> last;
    uint64_t id = 0, first = 0, count = 0;
    if (!ReadVarInt(aReader, id) || id > UINT32_MAX)
        return false;
    aRun.ActorId = static_cast<uint32_t>(id);
    if (!ReadVarInt(aReader, first) || first > UINT32_MAX || !ReadVarInt(aReader, count) || count > cMaxOps)
        return false;
    aRun.FirstUpdate = static_cast<uint32_t>(first);
    aRun.Ops.clear();
    aRun.Ops.reserve(count);
    uint32_t lastFrame = cNoFrame;
    while (aRun.Ops.size() < count)
    {
        uint64_t bit = 0;
        if (!aReader.ReadBits(bit, 1))
            return false;
        if (bit) // an update in the next frame that changes nothing, and its generate pass
        {
            if (lastFrame == cNoFrame || lastFrame + 1 >= aFrames || aRun.Ops.size() + 2 > count)
                return false;
            lastFrame = lastFrame + 1;
            aRun.Ops.push_back({.Frame = lastFrame});
            aRun.Ops.push_back({.Generate = true});
            continue;
        }
        GraphOp& op = aRun.Ops.emplace_back();
        bool generated = false;
        if (!aReader.ReadBits(bit, 1))
            return false;
        if (bit)
        {
            if (!aReader.ReadBits(bit, 1))
                return false;
            if (bit)
            {
                op.Generate = true;
                continue;
            }
            op.Event = true;
            uint64_t id = 0, load = 0, set = 0;
            if (!aReader.ReadBits(id, 16) || !aReader.ReadBits(load, 1) || (load && (!aReader.ReadBits(set, 8) || !set)))
                return false;
            op.EventId = static_cast<uint16_t>(id);
            op.ClipSet = static_cast<uint8_t>(set);
        }
        else
        {
            uint64_t next = 0, frame = 0, generate = 0;
            if (!aReader.ReadBits(next, 1))
                return false;
            if (next)
            {
                if (lastFrame == cNoFrame)
                    return false;
                frame = uint64_t{lastFrame} + 1;
            }
            else if (!ReadVarInt(aReader, frame))
                return false;
            if (frame >= aFrames || !aReader.ReadBits(generate, 1) || (generate && aRun.Ops.size() + 1 > count))
                return false;
            op.Frame = lastFrame = static_cast<uint32_t>(frame);
            generated = generate != 0;
        }
        if (!aReader.ReadBits(bit, 1))
            return false;
        if (bit)
        {
            uint64_t seed = 0, current = 0;
            if (!aReader.ReadBits(seed, 32) || !aReader.ReadBits(current, 32))
                return false;
            op.Seed = GraphSeed{static_cast<uint32_t>(seed), static_cast<uint32_t>(current)};
        }
        uint64_t changed = 0;
        if (!ReadVarInt(aReader, changed) || changed > cMaxChanged)
            return false;
        op.Changed.reserve(changed);
        uint64_t index = 0;
        for (uint64_t i = 0; i < changed; ++i)
        {
            uint64_t step = 0, mask = 0;
            if (!ReadVarInt(aReader, step) || (i && step == 0) || index + step > UINT16_MAX ||
                !aReader.ReadBits(mask, 4))
                return false;
            index += step;
            uint32_t xorValue = 0;
            for (int b = 0; b < 4; ++b)
            {
                uint64_t byte = 0;
                if ((mask & (1u << b)) && !aReader.ReadBits(byte, 8))
                    return false;
                xorValue |= static_cast<uint32_t>(byte) << (8 * b);
            }
            if (last.size() <= index)
                last.resize(index + 1);
            last[index] ^= xorValue;
            op.Changed.emplace_back(static_cast<uint16_t>(index), last[index]);
        }
        if (generated)
            aRun.Ops.push_back({.Generate = true}); // op is not used past here: the push may move it
    }
    return true;
}
