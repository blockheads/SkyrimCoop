#pragma once

#include <Messages/JoinMessages.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The host's save on its way to a guest (docs/design/join-flow.md, "Save sync"): its two files as a plain image, sent
// as its difference from an image the guest already holds (zstd, that image as the prefix). No engine.
namespace SaveTransfer
{
constexpr uint32_t kMaxSave = 256 * 1024 * 1024;

// A save's two files: the game's (.ess) and SKSE's co-save (.skse).
struct SaveFiles
{
    std::string Game;
    std::string CoSave;
    bool operator==(const SaveFiles&) const = default;
};

// Length-prefixed game file and co-save.
[[nodiscard]] std::string Pack(const SaveFiles& acFiles) noexcept;
// None when acPacked is not a packed save.
[[nodiscard]] std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept;
// SHA-256, 32 binary bytes.
[[nodiscard]] std::string Digest(std::string_view acBytes) noexcept;

// The game's file with its body raw (compression type 0, which the engine loads as it is: 109334) and its
// screenshot blank, so two saves of one game share most bytes. Anything else comes back unchanged.
[[nodiscard]] std::string Plain(std::string_view acGame) noexcept;

// A save as it travels and as a guest's store keeps it: the plain game file and the co-save, packed.
struct Image
{
    std::string Id;   // Data's SHA-256
    std::string Data;
};
// None when the save is too large.
[[nodiscard]] std::shared_ptr<const Image> MakeImage(const SaveFiles& acFiles) noexcept;

// acTarget as what it does not share with acBase; with no base, compressed alone. Empty when it cannot be.
[[nodiscard]] std::string Encode(std::string_view acTarget, std::string_view acBase = {}) noexcept;
// None unless acEncoded decodes against acBase to exactly aSize bytes.
[[nodiscard]] std::optional<std::string> Decode(std::string_view acEncoded, std::string_view acBase, uint32_t aSize) noexcept;

// An encoded image for the wire.
struct Stream
{
    std::string Save; // the image's id
    std::string Base; // the id of the image it is encoded against, or empty
    uint32_t Size{};  // the image's size
    std::string Data;

    // The piece at aOffset, at most NotifySaveChunk::kMaxData bytes.
    [[nodiscard]] NotifySaveChunk Piece(size_t aOffset, bool aAhead) const noexcept
    {
        NotifySaveChunk chunk;
        chunk.Save = Save;
        chunk.Base = Base;
        chunk.Size = Size;
        chunk.Total = static_cast<uint32_t>(Data.size());
        chunk.Offset = static_cast<uint32_t>(aOffset);
        chunk.Ahead = aAhead;
        chunk.Data = Data.substr(aOffset, NotifySaveChunk::kMaxData);
        return chunk;
    }
};

// A stream's pieces coming in, in order; a piece at offset 0 starts over (a newer save replaced the one on its way).
struct Assembly
{
    // False for a piece that does not continue the stream (another save's, out of order, out of bounds).
    bool Add(const NotifySaveChunk& acChunk) noexcept;
    [[nodiscard]] bool Complete() const noexcept { return m_total != 0 && m_stream.Data.size() == m_total; }
    [[nodiscard]] float Progress() const noexcept { return m_total ? static_cast<float>(m_stream.Data.size()) / static_cast<float>(m_total) : 0.f; }
    [[nodiscard]] const Stream& Get() const noexcept { return m_stream; }

private:
    Stream m_stream;
    uint32_t m_total{};
};
} // namespace SaveTransfer
