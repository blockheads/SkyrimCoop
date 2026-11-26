#include <physics/BodyPlayback.h>

#include <physics/BodyTolerances.h>

#include <algorithm>
#include <utility>

void BodyPlayback::Add(uint64_t aTick, const std::vector<BodyFrame>& acFrames) noexcept
{
    for (const auto& cFrame : acFrames)
    {
        if (const auto it = m_ended.find(cFrame.Key); it != m_ended.end() && aTick <= it->second)
            continue;
        Track& track = m_tracks[cFrame.Key];
        // Older than its rest, or than a frame already here (unreliable: out of order): the newer one stands.
        if ((track.Rest && aTick <= track.Rest->first) || (!track.Frames.empty() && aTick <= track.Frames.back().first))
            continue;
        track.Frames.emplace_back(aTick, cFrame);
        ++track.Received;
    }
}

void BodyPlayback::Rest(uint64_t aTick, const std::vector<BodyPose>& acRests) noexcept
{
    for (const auto& cRest : acRests)
    {
        if (const auto it = m_ended.find(cRest.Key); it != m_ended.end() && aTick <= it->second)
            continue;
        if (const auto it = m_tracks.find(cRest.Key); it != m_tracks.end() && it->second.Rest && it->second.Rest->first > aTick)
            continue;
        // The frames before it play on into it (At).
        m_tracks[cRest.Key].Rest = {aTick, cRest};
    }
}

namespace
{
float Seconds(uint64_t aFrom, uint64_t aTo) noexcept
{
    return aTo >= aFrom ? static_cast<float>(aTo - aFrom) / 1000.f : -static_cast<float>(aFrom - aTo) / 1000.f;
}
} // namespace

std::optional<BodyPlayback::Pose> BodyPlayback::Track::TakeRest(uint64_t aTick) noexcept
{
    if (!Rest || Rest->first > aTick)
        return std::nullopt;
    const auto [cRestTick, cRest] = *Rest;
    std::erase_if(Frames, [&](const auto& acFrame) { return acFrame.first <= cRestTick; });
    Rest.reset();
    return Pose{{.Key = cRest.Key, .Position = cRest.Position, .Rotation = cRest.Rotation, .Knocked = cRest.Knocked}, true, std::exchange(Received, 0), cRestTick};
}

void BodyPlayback::Track::Advance(uint64_t aTick) noexcept
{
    while (Frames.size() >= 2 && Frames[1].first <= aTick)
    {
        Frames.pop_front();
    }
}

BodyFrame BodyPlayback::Track::Show(uint64_t aTick, float aGravity) noexcept
{
    const auto& [cTick, cFrame] = Frames.front();
    // The next state (or the rest) known: where its authority had it.
    std::optional<std::pair<uint64_t, BodyFrame>> next;
    if (Rest && (Frames.size() < 2 || Rest->first < Frames[1].first))
        next = {Rest->first, BodyFrame{.Key = cFrame.Key, .Position = Rest->second.Position, .Rotation = Rest->second.Rotation}};
    else if (Frames.size() >= 2)
        next = Frames[1];
    // None yet: a free fall's arc is guessed on; on the ground it coasts kCoastMs and no further.
    if (!next)
    {
        const float cSeconds = Seconds(cTick, aTick);
        return BodyPrediction::Predict(cFrame, cFrame.Gravity ? cSeconds : std::min(cSeconds, static_cast<float>(BodyTolerances::kCoastMs) / 1000.f), aGravity);
    }
    const float cSpan = Seconds(cTick, next->first);
    if (cSpan <= 0.f)
        return next->second;
    const float s = std::clamp(Seconds(cTick, aTick) / cSpan, 0.f, 1.f);
    BodyFrame shown = cFrame;
    shown.Rotation = glm::normalize(glm::slerp(cFrame.Rotation, next->second.Rotation, s));
    shown.AngularVelocity = glm::mix(cFrame.AngularVelocity, next->second.AngularVelocity, s);
    const glm::vec3 cChord = next->second.Position - cFrame.Position;
    // On the ground (a slide, a hand's hold): along the line between the two, never past either; their speeds jump
    // with a hand or a contact, and a curve through them looped past the next (97 turn-backs on a carried object).
    if (!cFrame.Gravity || !next->second.Gravity)
    {
        shown.Position = cFrame.Position + cChord * s;
        shown.LinearVelocity = cChord / cSpan;
        return shown;
    }
    // A free fall: on the arc through both with their speeds (a cubic Hermite through two points of one arc is it).
    const glm::vec3& cFrom = cFrame.LinearVelocity;
    const glm::vec3& cTo = next->second.LinearVelocity;
    const float s2 = s * s, s3 = s2 * s;
    shown.Position = (2.f * s3 - 3.f * s2 + 1.f) * cFrame.Position + (s3 - 2.f * s2 + s) * cSpan * cFrom + (-2.f * s3 + 3.f * s2) * next->second.Position +
                     (s3 - s2) * cSpan * cTo;
    shown.LinearVelocity = (6.f * s2 - 6.f * s) / cSpan * cFrame.Position + (3.f * s2 - 4.f * s + 1.f) * cFrom + (-6.f * s2 + 6.f * s) / cSpan * next->second.Position +
                           (3.f * s2 - 2.f * s) * cTo;
    return shown;
}

std::vector<BodyPlayback::Pose> BodyPlayback::At(uint64_t aTick, float aGravity) noexcept
{
    return At([aTick](const BodyKey&) { return aTick; }, aGravity);
}

std::vector<BodyPlayback::Pose> BodyPlayback::At(const TickOf& acTickOf, float aGravity) noexcept
{
    std::vector<Pose> poses;
    for (auto it = m_tracks.begin(); it != m_tracks.end();)
    {
        const uint64_t aTick = acTickOf(it->first);
        if (!aTick)
        {
            ++it;
            continue;
        }
        Track& track = it->second;
        const uint64_t cRestTick = track.Rest ? track.Rest->first : 0;
        if (auto rest = track.TakeRest(aTick))
        {
            m_ended[it->first] = std::max(m_ended[it->first], cRestTick);
            poses.push_back(*rest);
        }
        track.Advance(aTick);
        if (!track.Showing(aTick))
        {
            // Nothing due yet; gone once it has neither frames nor a rest coming.
            it = !track.Rest && track.Frames.empty() ? m_tracks.erase(it) : std::next(it);
            continue;
        }
        if (track.IsLost(aTick))
        {
            m_lost.push_back({it->first, track.Received});
            it = m_tracks.erase(it);
            continue;
        }
        poses.push_back({track.Show(aTick, aGravity)});
        ++it;
    }
    return poses;
}

void BodyPlayback::Forget(const BodyKey& acKey) noexcept
{
    m_tracks.erase(acKey);
}

std::vector<BodyPlayback::Lost> BodyPlayback::TakeLost() noexcept
{
    return std::exchange(m_lost, {});
}

void BodyPlayback::ForgetUntil(const BodyKey& acKey, uint64_t aTick) noexcept
{
    Forget(acKey);
    m_ended[acKey] = std::max(m_ended[acKey], aTick);
}
