#include <Structs/GraphOps.h>
#include <Wire/Serialization.h>
#include <bit>

namespace
{
constexpr uint64_t cMaxFrames = 4096;
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

void GraphOps::WriteRun(Wire::Buffer::Writer& aWriter, const GraphOpsRun& acRun, GraphOpsHistory& aHistory)
{
    aWriter.WriteBits(acRun.ActorId, 32);
    Wire::Serialization::WriteVarInt(aWriter, acRun.FirstUpdate);
    Wire::Serialization::WriteVarInt(aWriter, acRun.Ops.size());
    uint32_t lastFrame = cNoFrame;
    for (const auto& cOp : acRun.Ops)
    {
        const bool cNext = !cOp.Event && lastFrame != cNoFrame && cOp.Frame == lastFrame + 1;
        const bool cEmpty = cNext && !cOp.Seed && cOp.Changed.empty();
        aWriter.WriteBits(cEmpty, 1);
        if (!cOp.Event)
            lastFrame = cOp.Frame;
        if (cEmpty)
            continue;
        aWriter.WriteBits(cOp.Event, 1);
        if (cOp.Event)
            aWriter.WriteBits(cOp.EventId, 16);
        else
        {
            aWriter.WriteBits(cNext, 1);
            if (!cNext)
                Wire::Serialization::WriteVarInt(aWriter, cOp.Frame);
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
            if (aHistory.Values.size() <= cIndex)
                aHistory.Values.resize(cIndex + 1);
            const uint32_t cXor = cValue ^ aHistory.Values[cIndex];
            aHistory.Values[cIndex] = cValue;
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

bool GraphOps::ReadRun(Wire::Buffer::Reader& aReader, GraphOpsRun& aRun, GraphOpsHistory& aHistory, size_t aFrames)
{
    uint64_t first = 0, count = 0;
    if (!ReadVarInt(aReader, first) || first > UINT32_MAX || !ReadVarInt(aReader, count) || count > cMaxOps)
        return false;
    aRun.FirstUpdate = static_cast<uint32_t>(first);
    aRun.Ops.assign(count, {});
    uint32_t lastFrame = cNoFrame;
    for (auto& op : aRun.Ops)
    {
        uint64_t bit = 0;
        if (!aReader.ReadBits(bit, 1))
            return false;
        if (bit) // an update in the next frame that changes nothing
        {
            if (lastFrame == cNoFrame || lastFrame + 1 >= aFrames)
                return false;
            op.Frame = lastFrame = lastFrame + 1;
            continue;
        }
        if (!aReader.ReadBits(bit, 1))
            return false;
        op.Event = bit;
        if (op.Event)
        {
            uint64_t id = 0;
            if (!aReader.ReadBits(id, 16))
                return false;
            op.EventId = static_cast<uint16_t>(id);
        }
        else
        {
            uint64_t next = 0, frame = 0;
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
            if (frame >= aFrames)
                return false;
            op.Frame = lastFrame = static_cast<uint32_t>(frame);
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
            if (aHistory.Values.size() <= index)
                aHistory.Values.resize(index + 1);
            aHistory.Values[index] ^= xorValue;
            op.Changed.emplace_back(static_cast<uint16_t>(index), aHistory.Values[index]);
        }
    }
    return true;
}
