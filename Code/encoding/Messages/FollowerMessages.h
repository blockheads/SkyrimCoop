#pragma once
#include <Messages/Message.h>
#include <Wire/Serialization.h>

struct NotifyFollowerTrade final : MessageOf<NotifyFollowerTrade, kNotifyFollowerTrade>
{
    uint32_t NpcId{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const NotifyFollowerTrade&) const noexcept = default;
};

struct FollowerTradeClosedRequest final : MessageOf<FollowerTradeClosedRequest, kFollowerTradeClosedRequest>
{
    uint32_t NpcId{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const FollowerTradeClosedRequest&) const noexcept = default;
};
