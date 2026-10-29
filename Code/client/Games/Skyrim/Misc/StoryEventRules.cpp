#include <Misc/StoryEventRules.h>

#include <cstring>

namespace StoryEventRules
{
namespace
{
bool IsNarrow(MemberType aType) noexcept
{
    return aType == MemberType::kReference || aType == MemberType::kInteger;
}
} // namespace

std::optional<std::vector<StoryEvent::Member>> ToSend(std::span<const MemberType> aTypes, std::span<const uint64_t> aMembers,
                                                      const std::function<Named(uint32_t aHandle)>& acReference,
                                                      const std::function<std::optional<GameId>(uint64_t aForm)>& acForm) noexcept
{
    using Member = StoryEvent::Member;
    std::vector<Member> members;
    for (size_t i = 0; i < aTypes.size() && i < aMembers.size() && i < StoryEvent::kMaxMembers; ++i)
    {
        Member& member = members.emplace_back();
        switch (aTypes[i])
        {
        case MemberType::kReference:
        {
            const Named cNamed = acReference(static_cast<uint32_t>(aMembers[i]));
            if (cNamed.Is == Named::kActor)
                return std::nullopt;
            if (cNamed.Is == Named::kThisPlayer)
                member.Type = Member::kCharacter;
            else if (cNamed.Is == Named::kObject)
                member = {Member::kReference, cNamed.Id};
            break;
        }
        case MemberType::kInteger: member = {.Type = Member::kInteger, .Value = static_cast<uint32_t>(aMembers[i])}; break;
        default:
            if (const auto cId = aMembers[i] ? acForm(aMembers[i]) : std::nullopt)
                member = {Member::kForm, *cId};
            break;
        }
    }
    return members;
}

std::optional<Raw> Layout(std::span<const MemberType> aTypes, std::span<const uint64_t> aValues) noexcept
{
    Raw raw{};
    if (aValues.size() > aTypes.size() || aTypes.size() > StoryEvent::kMaxMembers)
        return std::nullopt;
    auto* pBytes = reinterpret_cast<uint8_t*>(raw.data());
    size_t offset = 0;
    for (size_t i = 0; i < aTypes.size(); ++i)
    {
        const uint64_t cValue = i < aValues.size() ? aValues[i] : 0;
        if (IsNarrow(aTypes[i]))
        {
            const auto cNarrow = static_cast<uint32_t>(cValue);
            std::memcpy(pBytes + offset, &cNarrow, sizeof(cNarrow));
            offset += sizeof(cNarrow);
        }
        else
        {
            offset = (offset + 7) & ~size_t{7};
            std::memcpy(pBytes + offset, &cValue, sizeof(cValue));
            offset += sizeof(cValue);
        }
    }
    return raw;
}

bool Fits(MemberType aType, StoryEvent::Member::Kind aKind) noexcept
{
    using Member = StoryEvent::Member;
    if (aKind == Member::kNone)
        return true;
    switch (aType)
    {
    case MemberType::kReference: return aKind == Member::kCharacter || aKind == Member::kReference;
    case MemberType::kInteger: return aKind == Member::kInteger;
    default: return aKind == Member::kForm;
    }
}

std::string TypeName(uint32_t aType) noexcept
{
    std::string name(4, '\0');
    std::memcpy(name.data(), &aType, 4);
    return name;
}
} // namespace StoryEventRules
