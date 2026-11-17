#pragma once

#include <Bridges/IJoinBridge.h>

#include <TaskQueue.h>

#include <filesystem>
#include <memory>
#include <mutex>

/// IJoinBridge on the engine: its saves (BGSSaveLoadManager), its player, its main menu.
class JoinBridge final : public IJoinBridge
{
public:
    // The player base's change flags a record carries: all but Factions (the party's) and the AI and outfits.
    static constexpr uint32_t kBaseFlags = 0x03000E36; // BaseData Attributes SpellList FullName NPCSkills Class Face Gender Race

    JoinBridge() noexcept;
    ~JoinBridge() noexcept override;

    bool BeginCharacterCreation() noexcept override;
    void CancelCharacterCreation() noexcept override;
    Creation PollCharacterCreation() noexcept override;
    bool IsAtMainMenu() const noexcept override;
    void ReturnToMainMenu() noexcept override;
    std::string PlayerId() noexcept override;

    std::optional<SaveFiles> SaveNow(const std::string& acName) noexcept override;
    bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept override;

    // Save sync's work thread and files (JoinSaves.cpp).
    void OffThread(std::function<std::function<void()>()> aWork) noexcept override;
    void FinishWork() noexcept override;
    std::vector<std::string> HeldSaves() noexcept override;
    std::optional<std::string> ReadHeldSave(const std::string& acId) noexcept override;
    bool WriteHeldSave(const std::string& acId, const std::string& acData) noexcept override;
    void DropHeldSave(const std::string& acId) noexcept override;

    std::optional<SaveFiles> ReadSave(const std::string& acName) noexcept override;
    std::optional<std::string> PreviousSave() noexcept override;
    void SetCharacter(uint32_t aCharacterId) noexcept override;

    PlayerRecord ReadPlayer() const noexcept override;
    void ApplyPlayer(const PlayerRecord& acRecord) noexcept override;
    PlayerKnowledge ReadKnowledge() const noexcept override;
    void ApplyKnowledge(const PlayerKnowledge& acKnowledge) noexcept override;
    bool PlaceBesideHost() noexcept override;

private:
    // The save the main loop makes for SaveNow.
    struct Save
    {
        std::mutex Lock;
        bool Pending{};
        bool Done{};
        std::optional<SaveFiles> Files;
    };

    // One thread for the work a frame has no time for, started with the first of it; what the work returns waits in
    // m_finished for FinishWork.
    struct Worker;
    // My Games/Skyrim Special Edition/SkyrimCoop/worlds: a held save is <its id in hex>.world.
    [[nodiscard]] const std::filesystem::path& HeldFolder() noexcept;

    std::string m_playerId;
    std::shared_ptr<Save> m_pSave = std::make_shared<Save>();
    TaskQueue m_finished;
    std::unique_ptr<Worker> m_pWorker;
    std::once_flag m_heldFolderOnce;
    std::filesystem::path m_heldFolder;
};
