#include <Services/Host/JoinRefusal.h>

#include <spdlog/spdlog.h>

namespace JoinRefusal
{
using enum AuthenticationResponse::ResponseType;

JoinStatus ForPlayer(const AuthenticationResponse& acResponse, std::string_view aOurBuild) noexcept
{
    JoinStatus status;
    status.Current = JoinStatus::State::kRefused;
    switch (acResponse.Type)
    {
    case kWrongVersion:
        status.Reason = "wrong_build";
        status.Message = fmt::format("The host runs SkyrimCoop {}, you run {}. You both need the same build.", acResponse.Version.c_str(), aOurBuild);
        break;
    case kServerFull:
        status.Reason = "full";
        status.Message = "The session is full.";
        break;
    case kModsMismatch:
        status.Reason = "mods_differ";
        status.Message = "Your mods differ from the host's.";
        break;
    default:
        status.Reason = "refused";
        status.Message = "The host turned the join down.";
        break;
    }
    return status;
}

std::string ForHost(AuthenticationResponse::ResponseType aReason, std::string_view aName, std::string_view aTheirBuild,
                    std::string_view aOurBuild) noexcept
{
    const std::string_view cName = aName.empty() ? std::string_view("A player") : aName;
    switch (aReason)
    {
    case kWrongVersion: return fmt::format("{} couldn't join: they run SkyrimCoop {}, you run {}.", cName, aTheirBuild, aOurBuild);
    case kServerFull: return fmt::format("{} couldn't join: the session is full.", cName);
    case kModsMismatch: return fmt::format("{} couldn't join: their mods differ from yours.", cName);
    default: return fmt::format("{} couldn't join.", cName);
    }
}
} // namespace JoinRefusal
