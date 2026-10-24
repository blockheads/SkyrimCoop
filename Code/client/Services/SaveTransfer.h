#pragma once

#include <Messages/JoinMessages.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The host's save on its way to a game joining from the main menu (docs/design/join-flow.md, "As built"): its two
// files packed and compressed (snappy), sent in chunks, put back together. No engine.
namespace SaveTransfer
{
// A save's two files: the game's (.ess) and SKSE's co-save (.skse).
struct SaveFiles
{
    std::string Game;
    std::string CoSave;
    bool operator==(const SaveFiles&) const = default;
};

[[nodiscard]] std::string Pack(const SaveFiles& acFiles) noexcept;
// None when acPacked is not a packed save.
[[nodiscard]] std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept;
// The packed save as chunks of at most NotifySaveChunk::kMaxData bytes, in order.
[[nodiscard]] std::vector<NotifySaveChunk> Split(const std::string& acPacked) noexcept;

// Chunks coming in, in order.
struct Assembly
{
    // False for a chunk that does not continue the save (another save's, or out of order).
    bool Add(const NotifySaveChunk& acChunk) noexcept;
    [[nodiscard]] bool Complete() const noexcept { return m_total != 0 && m_data.size() == m_total; }
    [[nodiscard]] float Progress() const noexcept { return m_total ? static_cast<float>(m_data.size()) / static_cast<float>(m_total) : 0.f; }
    [[nodiscard]] const std::string& Data() const noexcept { return m_data; }

private:
    std::string m_data;
    uint32_t m_total{};
};
} // namespace SaveTransfer
