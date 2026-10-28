#pragma once

#include <Structs/PlayerRecord.h>

#include <map>
#include <unordered_map>
#include <optional>
#include <string>
#include <string_view>

/**
 * @brief The players' characters a save keeps (docs/design/join-flow.md, "As built"), by key: read from the loaded
 * save's co-save, kept up to date by the host's game from its guests, written into its saves. No engine.
 */
struct CharacterRecords
{
    // The co-save's record version (SKSE's per-record version).
    static constexpr uint32_t kVersion = 3;

    void Put(PlayerRecord aRecord) noexcept;
    [[nodiscard]] const PlayerRecord* Find(std::string_view aKey) const noexcept;
    [[nodiscard]] const std::map<std::string, PlayerRecord, std::less<>>& All() const noexcept { return m_records; }
    // A loaded record's place, from where the host's game shows that player now.
    void SetPlace(std::string_view aKey, const GameId& acWorldSpace, const GameId& acCell, const std::array<float, 3>& acPosition) noexcept;
    void Clear() noexcept { m_records.clear(); m_storySpells.clear(); }
    // Story spell grants/removals persist independently of which players were present.
    void SetStorySpell(const GameId& acSpell, bool aAdded) noexcept;
    void ApplyStorySpells(PlayerRecord& aRecord) const noexcept;
    // A player whose character the save no longer keeps.
    void Remove(std::string_view aKey) noexcept;

    // Every record, as the co-save holds them.
    [[nodiscard]] std::string Encode() const noexcept;
    // Replaces the records with acData's; false (and none) when it is not a record list of aVersion.
    bool Decode(std::string_view aData, uint32_t aVersion) noexcept;

private:
    std::map<std::string, PlayerRecord, std::less<>> m_records;
    std::unordered_map<GameId, bool> m_storySpells;
};
