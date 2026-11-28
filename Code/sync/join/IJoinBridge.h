#pragma once

#include <join/SaveList.h>
#include <join/SaveNames.h>
#include <join/SaveTransfer.h>
#include <Structs/PlayerRecord.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief The engine as joining a session needs it (docs/design/join-flow.md, "As built"): saves made, written
 * and loaded, this game's player as a character record, and the main menu.
 */
class IJoinBridge
{
public:
    using SaveFiles = SaveTransfer::SaveFiles;
    enum class Creation { kIdle, kStarting, kCustomizing, kFinalizing, kComplete, kFailed };

    // The bundled invite-only start. Poll also advances queued engine work; completion includes naming,
    // racial spells and the starting kit. Cancellation lets a menu already open finish normally.
    virtual bool BeginCharacterCreation() noexcept = 0;
    virtual void CancelCharacterCreation() noexcept = 0;
    [[nodiscard]] virtual Creation PollCharacterCreation() noexcept = 0;

    virtual ~IJoinBridge() = default;

    // The main menu is up (no save loaded).
    [[nodiscard]] virtual bool IsAtMainMenu() const noexcept = 0;
    // Return through the native quit flow, queued between frames.
    virtual void ReturnToMainMenu() noexcept = 0;
    // This player's id, made once and kept in its files ("id:" and 16 hex digits).
    [[nodiscard]] virtual std::string PlayerId() noexcept = 0;

    // A save under acName, its files read back: none until the engine has made it (on a main loop frame after the first
    // call; call again each update) or when it refused.
    [[nodiscard]] virtual std::optional<SaveFiles> SaveNow(const std::string& acName) noexcept = 0;
    // Writes a save's files under acName; the engine loads it on a main loop frame after (TESLoadGameEvent follows).
    // False when a file was not written: the other may be.
    virtual bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept = 0;

    // Work too heavy for a frame (a save imaged, encoded or decoded) runs off the game's threads; what it returns
    // runs in a later FinishWork, on the thread that calls it (JoinService's update).
    virtual void OffThread(std::function<std::function<void()>()> aWork) noexcept = 0;
    virtual void FinishWork() noexcept = 0;

    // The saves this game holds for save sync (docs/design/join-flow.md, "Save sync"), by id, outside the engine's
    // Saves folder; callable off thread. The ids, the newest first.
    [[nodiscard]] virtual std::vector<std::string> HeldSaves() noexcept = 0;
    [[nodiscard]] virtual std::optional<std::string> ReadHeldSave(const std::string& acId) noexcept = 0;
    // Written, or made the newest when it is there already.
    virtual bool WriteHeldSave(const std::string& acId, const std::string& acData) noexcept = 0;
    virtual void DropHeldSave(const std::string& acId) noexcept = 0;

    // A save and its co-save gone from the engine's Saves folder (nothing when there is none).
    virtual void DeleteSave(const std::string& acName) noexcept = 0;
    // An existing save and its co-save, selected by the host.
    [[nodiscard]] virtual std::optional<SaveFiles> ReadSave(const std::string& acName) noexcept = 0;
    // The engine's current character (its saves are filed and named under it), as a load of that character's save
    // sets it: a load of a scratch save (SkyrimCoopWipe) sets 0 (docs/re/systems/save-game.md).
    virtual void SetCharacter(SaveNames::Character aCharacter) noexcept = 0;
    [[nodiscard]] virtual uint32_t CurrentCharacter() const noexcept = 0;
    // Which saves the Journal's Load lists from now on (callable on any thread's behalf: the engine reads it on its
    // save thread).
    virtual void LimitSaveList(SaveList::Limit aLimit) noexcept = 0;
    // This game's player's character (no key, no place): docs/design/join-flow.md, "The character record".
    [[nodiscard]] virtual PlayerRecord ReadPlayer() const noexcept = 0;
    // Makes this game's player the record's character (the loaded save's player is another's), standing at the
    // record's place when it has one (a character kept in the host's save).
    virtual void ApplyPlayer(const PlayerRecord& acRecord) noexcept = 0;
    [[nodiscard]] virtual PlayerKnowledge ReadKnowledge() const noexcept = 0;
    virtual void ApplyKnowledge(const PlayerKnowledge& acKnowledge) noexcept = 0;
    // The loaded save left its player on the host's spot. Move beside it only after floor and clearance checks.
    // False while physics is unavailable or no spot is clear; leaves the player where it was.
    [[nodiscard]] virtual bool PlaceBesideHost() noexcept = 0;
};
