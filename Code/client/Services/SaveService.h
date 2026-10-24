#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Services/CharacterRecords.h>

#include <string>

struct World;
struct ConnectedEvent;

/**
 * @brief Saving in a session (docs/design/join-flow.md, "Saving: only the host"): a guest's game, and one joining a
 * session, refuses every save and says why; the host's game saves as usual, with every player's character in its
 * SKSE co-save (CharacterRecords).
 */
struct SaveService
{
    SaveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;

    NO_COPY_MOVE(SaveService);

    // The engine is about to save: false refuses it, and the chat says why (for the engine's own autosaves, once a
    // session).
    [[nodiscard]] bool OnSaveRequested(bool aAutosave = false) noexcept;
    // Whether this game may save now: not as a guest, nor while joining a session.
    [[nodiscard]] bool MaySave() const noexcept;

    // The players' characters the loaded save keeps, and the host's game keeps up to date.
    [[nodiscard]] CharacterRecords& GetRecords() noexcept { return m_records; }
    [[nodiscard]] const CharacterRecords& GetRecords() const noexcept { return m_records; }
    // SKSE's co-save callbacks: a save writes the records (GameSavingEvent first), a load reads them, a revert (a
    // new game, a load starting) drops them.
    [[nodiscard]] std::string WriteCoSave() noexcept;
    void ReadCoSave(std::string_view aData, uint32_t aVersion) noexcept;
    void RevertCoSave() noexcept;

    static constexpr const char* kRefusal = "Only the host saves: your game can't save while you're in a co-op session.";

private:
    void OnConnected(const ConnectedEvent&) noexcept { m_toldAutosave = false; }

    World& m_world;
    entt::dispatcher& m_dispatcher;
    CharacterRecords m_records;
    bool m_toldAutosave{};
    std::vector<entt::scoped_connection> m_connections;
};
