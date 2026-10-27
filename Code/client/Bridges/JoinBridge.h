#pragma once

#include <Bridges/IJoinBridge.h>

#include <memory>
#include <mutex>

/// IJoinBridge on the engine: its saves (BGSSaveLoadManager), its player, its main menu.
class JoinBridge final : public IJoinBridge
{
public:
    // The player base's change flags a record carries: all but Factions (the party's) and the AI and outfits.
    static constexpr uint32_t kBaseFlags = 0x03000E36; // BaseData Attributes SpellList FullName NPCSkills Class Face Gender Race

    bool IsAtMainMenu() const noexcept override;
    std::string PlayerId() noexcept override;

    std::optional<SaveFiles> SaveNow(const std::string& acName) noexcept override;
    bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept override;

    std::optional<SaveFiles> ReadSave(const std::string& acName) noexcept override;
    std::optional<SaveFiles> PreviousSave() noexcept override;

    PlayerRecord ReadPlayer() const noexcept override;
    void ApplyPlayer(const PlayerRecord& acRecord) noexcept override;

private:
    // The save the main loop makes for SaveNow.
    struct Save
    {
        std::mutex Lock;
        bool Pending{};
        bool Done{};
        std::optional<SaveFiles> Files;
    };

    std::string m_playerId;
    std::shared_ptr<Save> m_pSave = std::make_shared<Save>();
};
