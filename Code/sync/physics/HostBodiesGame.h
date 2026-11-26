#pragma once

#include <Structs/BodyFrame.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// The host's physics bodies since the last read (docs/design/physics-sync.md): those awake, read now, and those that
// fell asleep, where they rest.
struct HostBodyReads
{
    std::vector<BodyFrame> Awake;
    std::vector<BodyPose> Rested;
    std::vector<BodyKey> Held; // the host's player holds them
};

// What the host's rules read and change in its game: the host's physics bodies. A part of HostGame.
struct HostBodiesGame
{
    // One of the host's actors is dead now (a corpse a guest's player may move: HostBodies).
    std::function<bool(uint32_t aSessionId)> IsDead;
    // A body this game's physics does not simulate (a script holds it keyframed: an untripped tripwire, a trap that
    // has not fired): nobody's to take by a touch.
    std::function<bool(const BodyKey&)> IsKeyframed;
    // The host's world's physics bodies at aTick (the frame clock): awake ones read now, and those that came to rest.
    std::function<HostBodyReads(uint64_t aTick)> TakeBodies;
    // The bodies the host's player pushed, hit or dropped since the last call (PlayerHands).
    std::function<std::vector<BodyKey>()> TakeTouches;
    // The reference bodies Havok paired with each of those (IBodyBridge::ReadTouching).
    std::function<std::vector<std::pair<BodyKey, BodyKey>>(const std::vector<BodyKey>& acKeys)> ReadTouching;
    // A guest's authority over that body ended here: the host's game simulates it again where it is (its playback
    // ends, its states up to aUntil too, a hold's keyframe too).
    std::function<void(const BodyKey& acKey, uint64_t aUntil)> EndPlayback;
    // Where the host's game shows a guest's character (its copy), by player id; none without one.
    std::function<std::optional<glm::vec3>(uint32_t aPlayerId)> ReadPlayerPosition;
    // The host's bodies where they are now; none for one not here.
    std::function<std::vector<BodyPose>(const std::vector<BodyKey>& acKeys)> ReadBodies;
};
