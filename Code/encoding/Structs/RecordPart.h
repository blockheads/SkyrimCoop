#pragma once

#include <Wire/Archive.h>

#include <Wire/Buffer.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The kinds of part a copy record holds (docs/design/one-simulator.md, piece 1); each registered once, by its service.
enum class RecordKind : uint8_t
{
    kAiState = 1, // ActorAiState (AiStateService)
    kActorState = 2, // ActorStatePart (ActorStateService)
    kFactions = 3,   // Factions (ActorStateService)
    kName = 4,       // its display name (ActorStateService)
    kItems = 5,      // ItemsPart (InventoryService): a change carries only the stacks that changed
    kRide = 6,       // RidePart (CharacterService): a passenger's horse
    kCast = 7,       // CastPart (MagicService): the casts it holds
    kRagdoll = 8,    // RagdollPart (BodyService): where its ragdoll last came to rest
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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Wire::List(Parts, 0xFF)); }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    // The part of aKind, or none.
    [[nodiscard]] const RecordPart* Find(RecordKind aKind) const noexcept;

    std::vector<RecordPart> Parts;
};

// The part to hold once acChange arrives over apHeld: its bytes as they are, or for a kItems change of only some stacks,
// apHeld's merged with it. None: that change cannot be merged (no part held, or one other than the version just before):
// its owner is asked for the whole part (R3).
[[nodiscard]] std::optional<std::string> MergedPart(const RecordPart* apHeld, const RecordPart& acChange) noexcept;

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
