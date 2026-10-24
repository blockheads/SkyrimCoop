#include "UiModels.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataTypeRegister.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace Ui
{
Rml::String PlayerColors::ForName(const Rml::String& aName)
{
    if (const auto it = m_colors.find(aName); it != m_colors.end())
        return it->second;

    constexpr std::array palette{"#64b5ff", "#65f08c", "#ffad4a", "#c78aff", "#ff79be", "#54e1ef",
                                "#ffe36b", "#ff7d75", "#b7eb49", "#8f9dff", "#f5a5ff", "#4ef0c8"};
    // Spread the first few players across contrasting hues before using similar shades.
    if (m_colors.size() < palette.size())
        return m_colors.emplace(aName, palette[m_colors.size()]).first->second;

    uint32_t hash = 2166136261u;
    for (const unsigned char c : aName)
        hash = (hash ^ c) * 16777619u;
    const auto available = [&](const Rml::String& color) {
        return std::none_of(m_colors.begin(), m_colors.end(), [&](const auto& entry) { return entry.second == color; });
    };
    // Long sessions may encounter more names than the curated palette. Keep old colors stable and
    // generate additional light colors instead of reusing one already assigned to another name.
    for (uint32_t offset = 0;; ++offset)
    {
        const uint32_t rgb = hash + offset * 0x9e3779b9u;
        char color[8];
        std::snprintf(color, sizeof(color), "#%02x%02x%02x", 128u + ((rgb >> 14) & 127u), 128u + ((rgb >> 7) & 127u),
                      128u + (rgb & 127u));
        if (available(color))
            return m_colors.emplace(aName, color).first->second;
    }
}

Rml::String Member::Face()
{
    if (Health < 0.f)
        return "moods/neutral.png";
    if (Health <= 0.f)
        return "moods/dead.png";
    if (Health < 0.15f)
        return "moods/very-tired.png";
    if (Health < 0.30f)
        return "moods/tired.png";
    if (Health < 0.45f)
        return "moods/sad.png";
    if (Health < 0.60f)
        return "moods/neutral.png";
    if (Health < 0.75f)
        return "moods/happy.png";
    if (Health < 0.90f)
        return "moods/really-happy.png";
    return "moods/very-happy.png";
}

bool PartyModel::Bind(Rml::Context& aContext)
{
    auto model = aContext.CreateDataModel("party");
    if (!model)
        return false;
    auto member = model.RegisterStruct<Member>();
    if (!member || !member.RegisterMember("name", &Member::Name) || !member.RegisterMember("host", &Member::IsHost) ||
        !member.RegisterMember("face", &Member::Face) || !member.RegisterMember("level", &Member::Level) ||
        !member.RegisterMember("health", &Member::Health) || !member.RegisterMember("color", &Member::Color) ||
        !model.RegisterArray<Rml::Vector<Member>>() || !model.Bind("members", &Members) ||
        !model.Bind("visible", &Visible) || !model.Bind("opacity", &Opacity))
        return false;
    Handle = model.GetModelHandle();
    return true;
}

bool ChatModel::Bind(Rml::Context& aContext)
{
    auto model = aContext.CreateDataModel("chat");
    if (!model)
        return false;
    auto line = model.RegisterStruct<ChatLine>();
    if (!line || !line.RegisterMember("name", &ChatLine::Name) || !line.RegisterMember("text", &ChatLine::Text) ||
        !line.RegisterMember("system", &ChatLine::System) || !line.RegisterMember("color", &ChatLine::Color) ||
        !line.RegisterMember("perished", &ChatLine::Perished) ||
        !model.RegisterArray<Rml::Vector<ChatLine>>() || !model.Bind("lines", &Lines) || !model.Bind("open", &Open) ||
        !model.Bind("draft", &Draft))
        return false;
    Handle = model.GetModelHandle();
    return true;
}

bool JoinModel::Bind(Rml::Context& aContext)
{
    auto model = aContext.CreateDataModel("join");
    if (!model || !model.RegisterArray<Rml::Vector<Rml::String>>() || !model.Bind("visible", &Visible) ||
        !model.Bind("state", &State) || !model.Bind("step", &Step) || !model.Bind("completed", &Completed) ||
        !model.Bind("progress", &Progress) || !model.Bind("address", &Address) || !model.Bind("reason", &Reason) ||
        !model.Bind("message", &Message) || !model.Bind("details", &Details))
        return false;
    Handle = model.GetModelHandle();
    return true;
}

Rml::String JoinModel::CopyText() const
{
    Rml::String text = "state " + State + "\nreason " + Reason + "\nmessage " + Message;
    for (const auto& cDetail : Details)
        text += "\ndetail " + cDetail;
    return text;
}
} // namespace Ui
