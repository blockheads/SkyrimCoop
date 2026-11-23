#pragma once

#include <Physics/BodyPrediction.h>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// The target each body another game simulates is steered to, at a time behind the sender's
// (docs/design/physics-authority-plan.md, piece 4): between the two states around that time, on the curve through
// both with their speeds, never a guess past them. The next one not here: a free fall's arc is guessed on
// (BodyPrediction), a body on the ground coasts kCoastMs. A rest is handed over once it is due.
// No engine: ClientTests.
class BodyPlayback
{
public:
    // How long a body without frames or a rest is followed.
    static constexpr uint64_t kLostMs = 2000;

    struct Pose
    {
        BodyFrame Body{};  // where it shows and how it moves there (a rest: still)
        bool Rest{};       // placed exactly, no longer played
        uint32_t Frames{}; // a rest's: the frames that came since the last rest
        uint64_t Tick{};
    };
    struct Lost
    {
        BodyKey Key{};
        uint32_t Frames{};
    };

    void Add(uint64_t aTick, const std::vector<BodyFrame>& acFrames) noexcept;
    [[nodiscard]] bool Has(const BodyKey& acKey) const noexcept { return m_tracks.contains(acKey); }
    void Rest(uint64_t aTick, const std::vector<BodyPose>& acRests) noexcept;
    // A reference's body has a rest queued, not shown yet: that rest places it, not its move row.
    [[nodiscard]] bool RestComing(const GameId& acObject) const noexcept
    {
        for (const auto& [cKey, cTrack] : m_tracks)
            if (!cKey.Actor && cKey.Object == acObject && cTrack.Rest)
                return true;
        return false;
    }
    // The pose each body shows at aTick (the host's time, behind it): the moving ones each update, a rest once.
    [[nodiscard]] std::vector<Pose> At(uint64_t aTick, float aGravity = BodyPrediction::kGravity) noexcept;
    // The same, each body at its own time (a copy's ragdoll at its copy's); 0 shows nothing for it yet.
    using TickOf = std::function<uint64_t(const BodyKey&)>;
    [[nodiscard]] std::vector<Pose> At(const TickOf& acTickOf, float aGravity = BodyPrediction::kGravity) noexcept;
    void Forget(const BodyKey& acKey) noexcept;
    // A release already applied locally: queued older frames must not restart its playback.
    void ForgetUntil(const BodyKey& acKey, uint64_t aTick) noexcept;
    // The bodies left since the last call: no frame for kLostMs and no rest.
    [[nodiscard]] std::vector<Lost> TakeLost() noexcept;
    [[nodiscard]] size_t Count() const noexcept { return m_tracks.size(); }

private:
    struct Track
    {
        std::deque<std::pair<uint64_t, BodyFrame>> Frames; // ascending; the first is the one shown
        std::optional<std::pair<uint64_t, BodyPose>> Rest;
        uint32_t Received{}; // frames since the last rest

        // Its rest once due (the frames before it dropped), once.
        [[nodiscard]] std::optional<Pose> TakeRest(uint64_t aTick) noexcept;
        // To the newest frame due at aTick.
        void Advance(uint64_t aTick) noexcept;
        [[nodiscard]] bool Showing(uint64_t aTick) const noexcept { return !Frames.empty() && Frames.front().first <= aTick; }
        [[nodiscard]] bool IsLost(uint64_t aTick) const noexcept { return Frames.size() == 1 && aTick - Frames.front().first > kLostMs && !Rest; }
        // Where it shows at aTick, from the frame shown.
        [[nodiscard]] BodyFrame Show(uint64_t aTick, float aGravity) noexcept;
    };

    std::map<BodyKey, Track> m_tracks;
    std::map<BodyKey, uint64_t> m_ended;
    std::vector<Lost> m_lost;
};
