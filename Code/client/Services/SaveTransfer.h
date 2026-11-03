#pragma once

#include <Messages/JoinMessages.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The host's save on its way to a game joining from the main menu (docs/design/join-flow.md, "As built"): its two
// files packed, split into content-defined blocks compressed one by one (snappy), blocks the guest's last received
// save already has sent as their hash only. No engine.
namespace SaveTransfer
{
constexpr uint32_t kMaxSave = 128 * 1024 * 1024;
constexpr uint32_t kMinBlock = 8 * 1024;
constexpr uint32_t kMaxBlock = 24 * 1024;

// A save's two files: the game's (.ess) and SKSE's co-save (.skse).
struct SaveFiles
{
    std::string Game;
    std::string CoSave;
    bool operator==(const SaveFiles&) const = default;
};

// Length-prefixed game file and co-save. Blocks are compressed independently by Split.
[[nodiscard]] std::string Pack(const SaveFiles& acFiles) noexcept;
// None when acPacked is not a packed save.
[[nodiscard]] std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept;
// SHA-256, 32 binary bytes.
[[nodiscard]] std::string Digest(std::string_view acBytes) noexcept;
// The packed save as blocks of kMinBlock-kMaxBlock raw bytes, in order, each with its hash.
[[nodiscard]] std::vector<NotifySaveChunk> Split(const std::string& acPacked) noexcept;

// The previous received save, read from SkyrimCoopJoin on joining. No extra disk cache or stale world reuse.
struct Cache
{
    void Load(const SaveFiles& acFiles) noexcept;
    [[nodiscard]] std::vector<std::string> Hashes() const;
    [[nodiscard]] std::optional<std::string_view> Find(const std::string& acDigest) const noexcept;
    void Clear() noexcept;
private:
    std::string m_data;
    std::map<std::string, std::pair<size_t, size_t>> m_blocks;
};

// Blocks coming in, in order.
struct Assembly
{
    // False for a block that does not continue the save (another save's, out of order, corrupt, or a hash not cached).
    bool Add(const NotifySaveChunk& acChunk, const Cache* acpCache = nullptr) noexcept;
    [[nodiscard]] bool Complete() const noexcept { return m_total != 0 && m_data.size() == m_total; }
    [[nodiscard]] float Progress() const noexcept { return m_total ? static_cast<float>(m_data.size()) / static_cast<float>(m_total) : 0.f; }
    [[nodiscard]] const std::string& Data() const noexcept { return m_data; }
private:
    std::string m_data;
    uint32_t m_total{};
};
} // namespace SaveTransfer
