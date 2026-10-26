#pragma once

#include <Services/SaveTransfer.h>
#include <Structs/PlayerRecord.h>

#include <optional>
#include <string>

/**
 * @brief The engine as joining a session needs it (docs/design/join-flow.md, "As built"): saves made, written
 * and loaded, this game's player as a character record, and the main menu.
 */
class IJoinBridge
{
public:
    using SaveFiles = SaveTransfer::SaveFiles;

    virtual ~IJoinBridge() = default;

    // The main menu is up (no save loaded).
    [[nodiscard]] virtual bool IsAtMainMenu() const noexcept = 0;
    // This player's id, made once and kept in its files ("id:" and 16 hex digits).
    [[nodiscard]] virtual std::string PlayerId() noexcept = 0;

    // A save under acName, its files read back: none until the engine has made it (on a main loop frame after the first
    // call; call again each update) or when it refused.
    [[nodiscard]] virtual std::optional<SaveFiles> SaveNow(const std::string& acName) noexcept = 0;
    // Writes a save's files under acName; the engine loads it on a main loop frame after (TESLoadGameEvent follows).
    virtual bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept = 0;

    // Most recent gameplay save of the current host character, excluding internal transfer saves.
    [[nodiscard]] virtual std::optional<SaveFiles> PreviousSave() noexcept = 0;
    // This game's player's character (no key, no place): docs/design/join-flow.md, "The character record".
    [[nodiscard]] virtual PlayerRecord ReadPlayer() const noexcept = 0;
    // Makes this game's player the record's character (the loaded save's player is another's), standing at the
    // record's place when it has one (a character kept in the host's save).
    virtual void ApplyPlayer(const PlayerRecord& acRecord) noexcept = 0;
};
