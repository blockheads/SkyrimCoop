#pragma once

#include <Messages/JoinMessages.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Identical installs already have the map assets. Only the host's save and SKSE co-save travel.
namespace SaveTransfer
{
constexpr uint32_t kMaxSave = 128 * 1024 * 1024;
constexpr uint32_t kMinBlock = 8 * 1024;
constexpr uint32_t kMaxBlock = 24 * 1024;

struct SaveFiles
{
    std::string Game;
    std::string CoSave;
    bool operator==(const SaveFiles&) const = default;
};

// Length-prefixed game file and co-save. Blocks are compressed independently by Split.
[[nodiscard]] std::string Pack(const SaveFiles& acFiles) noexcept;
[[nodiscard]] std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept;
[[nodiscard]] std::string Digest(std::string_view acBytes) noexcept;
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

struct Assembly
{
    bool Add(const NotifySaveChunk& acChunk, const Cache* acpCache = nullptr) noexcept;
    [[nodiscard]] bool Complete() const noexcept { return m_total != 0 && m_data.size() == m_total; }
    [[nodiscard]] float Progress() const noexcept { return m_total ? static_cast<float>(m_data.size()) / static_cast<float>(m_total) : 0.f; }
    [[nodiscard]] const std::string& Data() const noexcept { return m_data; }
private:
    std::string m_data;
    uint32_t m_total{};
};
} // namespace SaveTransfer
