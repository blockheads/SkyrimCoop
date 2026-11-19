#pragma once

#include <Wire/Buffer.h>

#include <cstdint>
#include <string>
#include <vector>

// The kinds of part a copy record holds (docs/design/one-simulator.md, piece 1); each registered once, by its service.
enum class RecordKind : uint8_t
{
    kAiState = 1, // ActorAiState (AiStateService)
    kActorState = 2, // ActorStatePart (ActorStateService)
    kFactions = 3,   // Factions (ActorStateService)
    kName = 4,       // its display name (ActorStateService)
};

// One part of an actor's record as its owner last declared it: the owner's own counter and the part's bytes.
struct RecordPart
{
    bool operator==(const RecordPart&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    RecordKind Kind{};
    uint32_t Version{};
    std::string Bytes;
};

// Every part an actor's spawn carries.
struct RecordParts
{
    bool operator==(const RecordParts&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    // The part of aKind, or none.
    [[nodiscard]] const RecordPart* Find(RecordKind aKind) const noexcept;

    std::vector<RecordPart> Parts;
};

// A part's struct as a part's bytes, and back (false: not that struct's bytes).
template <class T> [[nodiscard]] std::string ToPartBytes(const T& acValue) noexcept
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    acValue.Serialize(writer);
    return {reinterpret_cast<const char*>(buffer.GetData()), writer.Size()};
}

template <class T> [[nodiscard]] bool FromPartBytes(const std::string& acBytes, T& aValue) noexcept
{
    if (acBytes.empty())
        return false;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(acBytes.data()), acBytes.size()));
    aValue.Deserialize(reader);
    return true;
}
