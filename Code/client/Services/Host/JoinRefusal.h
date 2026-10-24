#pragma once

#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Services/JoinStatus.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

#include <string>
#include <string_view>

// Why the host's gate refused a game (docs/design/join-flow.md, "Errors"): the wire carries the
// reason and its data, and these word it.
namespace JoinRefusal
{
// What a game runs, as the gate compares it.
struct Install
{
    std::string_view Build;
    GameVersions Game;
    Mods Plugins;
};

// A join that ended on this game's side: unreachable, lost_connection or transfer_failed.
JoinStatus Failed(std::string_view aReason) noexcept;
// A guest's session that ended: lost_connection (after its reconnect tries) or kicked.
JoinStatus Ended(std::string_view aReason) noexcept;
// The refused game's status, against its own install.
JoinStatus ForPlayer(const AuthenticationResponse& acResponse, const Install& acOurs) noexcept;
// The line the host's chat shows: the game that sent acRequest was refused.
std::string ForHost(AuthenticationResponse::ResponseType aReason, const AuthenticationRequest& acRequest, const Install& acOurs) noexcept;
} // namespace JoinRefusal
