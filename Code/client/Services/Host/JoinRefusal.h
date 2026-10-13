#pragma once

#include <Messages/AuthenticationResponse.h>
#include <Services/JoinStatus.h>

#include <string>
#include <string_view>

// Why the host's gate refused a game (docs/design/join-flow.md, "Errors"): the wire carries the
// reason and its data, and these word it.
namespace JoinRefusal
{
// The refused game's status; aOurBuild is its own build.
JoinStatus ForPlayer(const AuthenticationResponse& acResponse, std::string_view aOurBuild) noexcept;
// The line the host's chat shows: aName, on aTheirBuild, was refused.
std::string ForHost(AuthenticationResponse::ResponseType aReason, std::string_view aName, std::string_view aTheirBuild,
                    std::string_view aOurBuild) noexcept;
} // namespace JoinRefusal
