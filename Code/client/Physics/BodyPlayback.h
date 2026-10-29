#pragma once

#include <Physics/BodyPrediction.h>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// A guest's playback of the host's bodies (docs/design/physics-sync.md, "How a guest applies it"), at a time behind
// the host's. Each body shows the host's guess from its last frame (BodyPrediction), and:
//
//   next frame here early   blended into it, arriving on it when it is due (the guest is behind: the usual case)
//   a frame came late       its correction (where the old guess was) blended out over kBlendMs instead of a jump
//   first frame             blended from where this game showed it (StartFrom: its own physics moved it first)
//   rest                    blended into like a frame, then placed exactly, once
//
// No engine: ClientTests.
class BodyPlayback
{
public:
    // How long a correction takes to blend in, and how long a body without frames or a rest is followed.
    static constexpr uint64_t kBlendMs = 100;
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
    // A body not played yet starts from where this game shows it (its own physics moved it), blended out over kBlendMs.
    void StartFrom(const BodyPose& acShown) noexcept;
    [[nodiscard]] bool Has(const BodyKey& acKey) const noexcept { return m_tracks.contains(acKey); }
    void Rest(uint64_t aTick, const std::vector<BodyPose>& acRests) noexcept;
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
        // The shown frame's start: where the last frame's guess was then, blended out over kBlendMs (a frame that
        // came too late to blend toward).
        glm::vec3 Offset{};
        glm::quat RotationOffset{1.f, 0.f, 0.f, 0.f};
        bool Started{};
        uint64_t Toward{}; // since then the next frame (or the rest) is known: shown blending into it, 0 not yet
        std::optional<BodyPose> From; // where this game showed it before its first frame
        uint32_t Received{}; // frames since the last rest

        // Its rest once due (the frames before it dropped), once.
        [[nodiscard]] std::optional<Pose> TakeRest(uint64_t aTick) noexcept;
        // To the newest frame due at aTick.
        void Advance(uint64_t aTick, float aGravity) noexcept;
        [[nodiscard]] bool Showing(uint64_t aTick) const noexcept { return !Frames.empty() && Frames.front().first <= aTick; }
        [[nodiscard]] bool IsLost(uint64_t aTick) const noexcept { return Frames.size() == 1 && aTick - Frames.front().first > kLostMs && !Rest; }
        // Where it shows at aTick, from the frame shown.
        [[nodiscard]] BodyFrame Show(uint64_t aTick, float aGravity) noexcept;
    };

    std::map<BodyKey, Track> m_tracks;
    std::map<BodyKey, uint64_t> m_ended;
    std::vector<Lost> m_lost;
};
