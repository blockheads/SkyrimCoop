#include <Services/Host/JoinRefusal.h>

#include <Structs/LoadOrder.h>

#include <spdlog/spdlog.h>

namespace JoinRefusal
{
using enum AuthenticationResponse::ResponseType;

namespace
{
// "Skyrim 1.6.1170 with SKSE 2.2.6"
std::string Describe(const GameVersions& acGame)
{
    return std::format("Skyrim {} with SKSE {}", GameVersions::Format(acGame.Runtime), GameVersions::Format(acGame.Skse));
}

// One line per difference, from the player's side: what they lack, have extra, or load differently.
std::vector<std::string> Describe(const std::vector<LoadOrderDifference>& acDifferences)
{
    using enum LoadOrderDifference::Kind;
    std::vector<std::string> lines;
    for (const auto& cDifference : acDifferences)
    {
        switch (cDifference.What)
        {
        case kMissing: lines.push_back(std::format("missing {}", cDifference.Plugin)); break;
        case kExtra: lines.push_back(std::format("extra {}", cDifference.Plugin)); break;
        case kLight:
            lines.push_back(std::format("light {}: the host loads it as {} plugin, you as {}", cDifference.Plugin, cDifference.Light ? "a light" : "a full",
                                        cDifference.Light ? "a full one" : "a light one"));
            break;
        case kOrder:
            lines.push_back(std::format("order: {} the host loads {}, you load {}",
                                        cDifference.After.empty() ? std::string("first") : std::format("after {}", cDifference.After), cDifference.Plugin,
                                        cDifference.Theirs));
            break;
        }
    }
    return lines;
}
} // namespace

JoinStatus Failed(std::string_view aReason) noexcept
{
    JoinStatus status;
    status.Current = JoinStatus::State::kRefused;
    status.Reason = aReason;
    if (aReason == "unreachable")
        status.Message = "Couldn't reach the host's game. Check the address, or that the host is still hosting.";
    else if (aReason == "lost_connection")
        status.Message = "The connection to the host dropped while joining. Try again.";
    else if (aReason == "transfer_failed")
        status.Message = "The host's world didn't arrive or didn't load. Try joining again.";
    else if (aReason == "placement_failed")
        status.Message = "Couldn't find room beside the host. Ask the host to move to an open spot, then join again.";
    else if (aReason == "no_character")
        status.Message = "The host's game has no character of yours yet: start or load a game of your own, then join from it.";
    else
        status.Message = "The join didn't finish.";
    return status;
}

JoinStatus Ended(std::string_view aReason) noexcept
{
    JoinStatus status;
    status.Current = JoinStatus::State::kEnded;
    status.Reason = aReason;
    if (aReason == "kicked")
        status.Message = "The host removed you from the session.";
    else if (aReason == "host_left")
        status.Message = "The host ended the session.";
    else
    {
        status.Message = "Lost connection to the host.";
        status.Details = {"Reconnecting didn't work.", "Join again through Steam, or restart the game."};
    }
    return status;
}

JoinStatus ForPlayer(const AuthenticationResponse& acResponse, const Install& acOurs) noexcept
{
    JoinStatus status;
    status.Current = JoinStatus::State::kRefused;
    switch (acResponse.Type)
    {
    case kWrongVersion:
        status.Reason = "wrong_build";
        status.Message = std::format("The host runs SkyrimCoop {}, you run {}. You both need the same build.", acResponse.Version.c_str(), acOurs.Build);
        break;
    case kWrongGame:
        status.Reason = "wrong_game";
        status.Message = std::format("The host runs {}, you run {}. You both need the same versions.", Describe(acResponse.Game), Describe(acOurs.Game));
        break;
    case kModsMismatch:
        status.Reason = "mods_differ";
        status.Details = Describe(CompareLoadOrders(acResponse.UserMods, acOurs.Plugins));
        status.Message = std::format("Your plugins differ from the host's ({} {}). You both need the same plugins in the same order.", status.Details.size(),
                                     status.Details.size() == 1 ? "difference" : "differences");
        break;
    case kSessionFull:
        status.Reason = "full";
        status.Message = "The session is full.";
        break;
    case kTransferFailed: return Failed("transfer_failed");
    case kNoCharacter: return Failed("no_character");
    default:
        status.Reason = "refused";
        status.Message = "The host turned the join down.";
        break;
    }
    return status;
}

std::string ForHost(AuthenticationResponse::ResponseType aReason, const AuthenticationRequest& acRequest, const Install& acOurs) noexcept
{
    const std::string_view cName = acRequest.Username.empty() ? std::string_view("A player") : std::string_view(acRequest.Username.c_str());
    switch (aReason)
    {
    case kWrongVersion: return std::format("{} couldn't join: they run SkyrimCoop {}, you run {}.", cName, acRequest.Version.c_str(), acOurs.Build);
    case kWrongGame: return std::format("{} couldn't join: they run {}, you run {}.", cName, Describe(acRequest.Game), Describe(acOurs.Game));
    case kModsMismatch:
    {
        const size_t cCount = CompareLoadOrders(acOurs.Plugins, acRequest.UserMods).size();
        return std::format("{} couldn't join: their plugins differ from yours ({} {}).", cName, cCount, cCount == 1 ? "difference" : "differences");
    }
    case kSessionFull: return std::format("{} couldn't join: the session is full.", cName);
    case kTransferFailed: return std::format("{} couldn't join: your game could not save for them.", cName);
    case kNoCharacter: return std::format("{} couldn't join: they have no character in your game yet (the first time, they join from a game of their own).", cName);
    default: return std::format("{} couldn't join.", cName);
    }
}
} // namespace JoinRefusal
