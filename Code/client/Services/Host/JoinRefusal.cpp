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
    return fmt::format("Skyrim {} with SKSE {}", GameVersions::Format(acGame.Runtime), GameVersions::Format(acGame.Skse));
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
        case kMissing: lines.push_back(fmt::format("missing {}", cDifference.Plugin)); break;
        case kExtra: lines.push_back(fmt::format("extra {}", cDifference.Plugin)); break;
        case kLight:
            lines.push_back(fmt::format("light {}: the host loads it as {} plugin, you as {}", cDifference.Plugin, cDifference.Light ? "a light" : "a full",
                                        cDifference.Light ? "a full one" : "a light one"));
            break;
        case kOrder:
            lines.push_back(fmt::format("order: {} the host loads {}, you load {}",
                                        cDifference.After.empty() ? std::string("first") : fmt::format("after {}", cDifference.After), cDifference.Plugin,
                                        cDifference.Theirs));
            break;
        }
    }
    return lines;
}
} // namespace

JoinStatus ForPlayer(const AuthenticationResponse& acResponse, const Install& acOurs) noexcept
{
    JoinStatus status;
    status.Current = JoinStatus::State::kRefused;
    switch (acResponse.Type)
    {
    case kWrongVersion:
        status.Reason = "wrong_build";
        status.Message = fmt::format("The host runs SkyrimCoop {}, you run {}. You both need the same build.", acResponse.Version.c_str(), acOurs.Build);
        break;
    case kWrongGame:
        status.Reason = "wrong_game";
        status.Message = fmt::format("The host runs {}, you run {}. You both need the same versions.", Describe(acResponse.Game), Describe(acOurs.Game));
        break;
    case kModsMismatch:
        status.Reason = "mods_differ";
        status.Details = Describe(CompareLoadOrders(acResponse.UserMods, acOurs.Plugins));
        status.Message = fmt::format("Your plugins differ from the host's ({} {}). You both need the same plugins in the same order.", status.Details.size(),
                                     status.Details.size() == 1 ? "difference" : "differences");
        break;
    case kServerFull:
        status.Reason = "full";
        status.Message = "The session is full.";
        break;
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
    case kWrongVersion: return fmt::format("{} couldn't join: they run SkyrimCoop {}, you run {}.", cName, acRequest.Version.c_str(), acOurs.Build);
    case kWrongGame: return fmt::format("{} couldn't join: they run {}, you run {}.", cName, Describe(acRequest.Game), Describe(acOurs.Game));
    case kModsMismatch:
    {
        const size_t cCount = CompareLoadOrders(acOurs.Plugins, acRequest.UserMods).size();
        return fmt::format("{} couldn't join: their plugins differ from yours ({} {}).", cName, cCount, cCount == 1 ? "difference" : "differences");
    }
    case kServerFull: return fmt::format("{} couldn't join: the session is full.", cName);
    default: return fmt::format("{} couldn't join.", cName);
    }
}
} // namespace JoinRefusal
