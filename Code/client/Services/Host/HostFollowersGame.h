#pragma once

#include <Followers/State.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// What the host's rules read and change in its game: followers, as the host's game holds them. A part of HostGame.
struct HostFollowersGame
{
    // Vanilla follower aliases and completed native commands; only the host save keeps their desired anchors.
    std::function<std::optional<std::vector<Followers::State>>()> ReadFollowers;
    std::function<std::vector<Followers::Order>()> TakeFollowerOrders;
    std::function<Followers::Anchors&()> FollowerAnchors;
    std::function<void(const std::vector<Followers::Target>&)> SetFollowerTargets;
};
