#include <Physics/BodyPlayback.h>

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

void BodyPlayback::StartFrom(const BodyPose& acShown) noexcept
{
    Track& track = m_tracks[acShown.Key];
    if (!track.Started)
        track.From = acShown;
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
// a moved toward b by aWeight, its velocities too.
BodyFrame Blend(const BodyFrame& acA, const BodyFrame& acB, float aWeight) noexcept
{
    BodyFrame blend = acA;
    blend.Position = glm::mix(acA.Position, acB.Position, aWeight);
    blend.Rotation = glm::normalize(glm::slerp(acA.Rotation, acB.Rotation, aWeight));
    blend.LinearVelocity = glm::mix(acA.LinearVelocity, acB.LinearVelocity, aWeight);
    blend.AngularVelocity = glm::mix(acA.AngularVelocity, acB.AngularVelocity, aWeight);
    return blend;
}

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
    Started = false;
    Toward = 0;
    return Pose{{.Key = cRest.Key, .Position = cRest.Position, .Rotation = cRest.Rotation, .Knocked = cRest.Knocked}, true, std::exchange(Received, 0), cRestTick};
}

void BodyPlayback::Track::Advance(uint64_t aTick, float aGravity) noexcept
{
    while (Frames.size() >= 2 && Frames[1].first <= aTick)
    {
        const auto& [cOldTick, cOld] = Frames[0];
        const auto& [cNewTick, cNew] = Frames[1];
        // Blended into ahead, it stands on the new frame now; came too late, it leaves where the old guess was.
        Offset = {};
        RotationOffset = glm::quat(1.f, 0.f, 0.f, 0.f);
        if (Started && !Toward)
        {
            const BodyFrame cGuess = BodyPrediction::Predict(cOld, Seconds(cOldTick, cNewTick), aGravity);
            Offset = cGuess.Position - cNew.Position;
            RotationOffset = cGuess.Rotation * glm::inverse(cNew.Rotation);
        }
        Toward = 0;
        Frames.pop_front();
    }
}

BodyFrame BodyPlayback::Track::Show(uint64_t aTick, float aGravity) noexcept
{
    const auto& [cTick, cFrame] = Frames.front();
    BodyFrame shown = BodyPrediction::Predict(cFrame, Seconds(cTick, aTick), aGravity);

    // Its first frame: from where this game showed it.
    if (const auto cFrom = std::exchange(From, std::nullopt); cFrom && !Started)
    {
        Offset = cFrom->Position - shown.Position;
        RotationOffset = cFrom->Rotation * glm::inverse(shown.Rotation);
        Started = true;
    }
    // A correction left from a late frame or its first, blended out.
    if (const float cLeft = Started ? 1.f - std::min(1.f, Seconds(cTick, aTick) * 1000.f / kBlendMs) : 0.f; cLeft > 0.f)
    {
        shown.Position += Offset * cLeft;
        shown.Rotation = glm::normalize(glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), RotationOffset, cLeft) * shown.Rotation);
    }
    // The next frame (or the rest) here before it is due: blended into it from now, on it when it is due.
    std::optional<std::pair<uint64_t, BodyFrame>> next;
    if (Rest && (Frames.size() < 2 || Rest->first < Frames[1].first))
        next = {Rest->first, BodyFrame{.Key = cFrame.Key, .Position = Rest->second.Position, .Rotation = Rest->second.Rotation}};
    else if (Frames.size() >= 2)
        next = {Frames[1].first, BodyPrediction::Predict(Frames[1].second, Seconds(Frames[1].first, aTick), aGravity)};
    if (next && Started)
    {
        if (!Toward)
            Toward = aTick;
        const float cSpan = Seconds(Toward, next->first);
        shown = Blend(shown, next->second, cSpan > 0.f ? std::clamp(Seconds(Toward, aTick) / cSpan, 0.f, 1.f) : 1.f);
    }
    Started = true;
    return shown;
}

std::vector<BodyPlayback::Pose> BodyPlayback::At(uint64_t aTick, float aGravity) noexcept
{
    std::vector<Pose> poses;
    for (auto it = m_tracks.begin(); it != m_tracks.end();)
    {
        Track& track = it->second;
        const uint64_t cRestTick = track.Rest ? track.Rest->first : 0;
        if (auto rest = track.TakeRest(aTick))
        {
            m_ended[it->first] = std::max(m_ended[it->first], cRestTick);
            poses.push_back(*rest);
        }
        track.Advance(aTick, aGravity);
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
