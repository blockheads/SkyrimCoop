#pragma once

using Wire::Buffer;

struct GameId
{
    GameId() = default;
    GameId(uint32_t aModId, uint32_t aBaseId) noexcept;
    ~GameId() = default;

    bool operator==(const GameId& acRhs) const noexcept;
    bool operator!=(const GameId& acRhs) const noexcept;

    operator bool() const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
    inline uint64_t LogFormat() const noexcept { return static_cast<uint64_t>(ModId) << 32 | BaseId; }

    uint32_t BaseId;
    uint32_t ModId;
};

namespace std
{
template <> class hash<GameId>
{
public:
    size_t operator()(const GameId& gameId) const { return hash<uint32_t>()(gameId.BaseId) ^ (hash<uint32_t>()(gameId.ModId) << 1); }
};
} // namespace std
