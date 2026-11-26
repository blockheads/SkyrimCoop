#pragma once

#include <Services/SaveTransfer.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// What the host's rules read and change in its game: what a joining game is told and sent. A part of HostGame.
struct HostJoinGame
{
    // Saves the host's game now for a game joining from the main menu: the save's image, by its id; none while the
    // engine has not made it (HostSaveTransfer asks again next update), an empty id when it cannot be sent.
    std::function<std::optional<std::string>()> SaveWorld;
    // A save's image encoded for a guest whose store holds acHeld: against the newest of them the host still has,
    // else whole. None while it is being encoded (asked again next update); no data when the image is gone.
    std::function<std::shared_ptr<const SaveTransfer::Stream>(const std::string& acSave, const std::vector<std::string>& acHeld)> EncodeSave;
    std::function<bool(const std::string& acKey)> HasRecord;
    // The host's difficulty (0 Novice to 5 Legendary): the session's.
    std::function<uint32_t()> ReadDifficulty;
    // The host's Skyrim and SKSE, and its plugins in load order: every player's must be the same.
    GameVersions Versions;
    Mods Plugins;
};
