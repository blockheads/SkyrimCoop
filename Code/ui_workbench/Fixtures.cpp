#include "Fixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
using Json = nlohmann::json;

bool String(const Json& acObject, const char* acKey, Rml::String& aValue)
{
    if (!acObject.contains(acKey))
        return true;
    if (!acObject[acKey].is_string())
        return false;
    aValue = acObject[acKey].get<std::string>();
    return true;
}

bool Boolean(const Json& acObject, const char* acKey, bool& aValue)
{
    if (!acObject.contains(acKey))
        return true;
    if (!acObject[acKey].is_boolean())
        return false;
    aValue = acObject[acKey].get<bool>();
    return true;
}

template <class T> bool Number(const Json& acObject, const char* acKey, T& aValue, float aMinimum, float aMaximum)
{
    if (!acObject.contains(acKey))
        return true;
    if (!acObject[acKey].is_number())
        return false;
    const double value = acObject[acKey].get<double>();
    if (!std::isfinite(value) || value < aMinimum || value > aMaximum ||
        (std::is_integral_v<T> && std::floor(value) != value))
        return false;
    aValue = static_cast<T>(value);
    return true;
}

bool Parse(const Json& acData, Ui::PartyModel& aParty, Ui::ChatModel& aChat, Ui::JoinModel& aJoin, Ui::DeathModel& aDeath)
{
    if (!acData.is_object())
        return false;
    if (!Boolean(acData, "party_visible", aParty.Visible) || !Number(acData, "party_opacity", aParty.Opacity, 0, 1))
        return false;
    if (acData.contains("party"))
    {
        if (!acData["party"].is_array())
            return false;
        for (const auto& entry : acData["party"])
        {
            Ui::Member member;
            if (!entry.is_object() || !String(entry, "name", member.Name) || !Boolean(entry, "host", member.IsHost) ||
                !Number(entry, "level", member.Level, 0, 1000) || !Number(entry, "health", member.Health, -1, 1))
                return false;
            aParty.Members.push_back(std::move(member));
        }
    }
    if (acData.contains("chat"))
    {
        const auto& chat = acData["chat"];
        if (!chat.is_object() || !Boolean(chat, "open", aChat.Open) || !String(chat, "draft", aChat.Draft))
            return false;
        if (chat.contains("lines"))
        {
            if (!chat["lines"].is_array())
                return false;
            for (const auto& entry : chat["lines"])
            {
                Ui::ChatLine line;
                if (!entry.is_object() || !String(entry, "name", line.Name) || !String(entry, "text", line.Text) ||
                    !Boolean(entry, "system", line.System) || !Boolean(entry, "perished", line.Perished))
                    return false;
                aChat.Lines.push_back(std::move(line));
            }
        }
    }
    if (acData.contains("join"))
    {
        const auto& join = acData["join"];
        if (!join.is_object() || !Boolean(join, "visible", aJoin.Visible) || !String(join, "state", aJoin.State) ||
            !Number(join, "step", aJoin.Step, 0, 6) || !Number(join, "completed", aJoin.Completed, 0, 6) ||
            !Number(join, "progress", aJoin.Progress, -1, 1) || !String(join, "address", aJoin.Address) ||
            !String(join, "reason", aJoin.Reason) || !String(join, "message", aJoin.Message))
            return false;
        constexpr std::array states{"idle",    "connecting", "checking", "receiving_world", "creating_character",
                                    "loading", "joined",     "hosting",  "refused",         "ended",
                                    "reconnecting"};
        if (std::find(states.begin(), states.end(), aJoin.State) == states.end())
            return false;
        if (join.contains("details"))
        {
            if (!join["details"].is_array())
                return false;
            for (const auto& detail : join["details"])
            {
                if (!detail.is_string())
                    return false;
                aJoin.Details.push_back(detail.get<std::string>());
            }
        }
        if ((aJoin.State == "refused" || aJoin.State == "ended") && (aJoin.Reason.empty() || aJoin.Message.empty()))
            return false;
    }
    if (acData.contains("death"))
    {
        const auto& death = acData["death"];
        if (!death.is_object() || !Boolean(death, "visible", aDeath.Visible) ||
            !String(death, "name", aDeath.Name) ||
            !String(death, "status", aDeath.Status)) return false;
    }
    return true;
}
} // namespace

bool LoadFixture(const std::filesystem::path& acPath, Ui::PartyModel& aParty, Ui::ChatModel& aChat,
                 Ui::JoinModel& aJoin, Ui::DeathModel& aDeath, Ui::PlayerColors& aColors)
{
    std::ifstream stream(acPath);
    Ui::PartyModel party;
    Ui::ChatModel chat;
    Ui::JoinModel join;
    Ui::DeathModel death;
    const auto data = Json::parse(stream, nullptr, false);
    if (!stream || !Parse(data, party, chat, join, death))
    {
        std::fprintf(stderr, "Invalid UI fixture: %s\n", acPath.c_str());
        return false;
    }
    aParty.Members = std::move(party.Members);
    aParty.Visible = party.Visible;
    aParty.Opacity = party.Opacity;
    for (auto& member : aParty.Members)
        member.Color = aColors.ForName(member.Name);
    aChat.Lines = std::move(chat.Lines);
    for (auto& line : aChat.Lines)
        if (!line.System)
            line.Color = aColors.ForName(line.Name);
    aChat.Open = chat.Open;
    aChat.Draft = std::move(chat.Draft);
    const auto handle = aJoin.Handle;
    aJoin = std::move(join);
    aJoin.Handle = handle;
    aParty.Handle.DirtyAllVariables();
    aChat.Handle.DirtyAllVariables();
    aJoin.Handle.DirtyAllVariables();
    const auto deathHandle = aDeath.Handle;
    aDeath = std::move(death);
    aDeath.Handle = deathHandle;
    aDeath.Handle.DirtyAllVariables();
    return true;
}
