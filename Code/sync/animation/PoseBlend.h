#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

// A bone's transform as Havok keeps it (hkQsTransform): translation, rotation (x, y, z, w), scale.
struct PoseBone
{
    float Translation[4]{};
    float Rotation[4]{0.f, 0.f, 0.f, 1.f};
    float Scale[4]{1.f, 1.f, 1.f, 1.f};
};
static_assert(sizeof(PoseBone) == 48);

// A streamed copy's last generated poses by its owner's time, shown blended a little behind playback, so its pose moves
// every frame however its owner's updates fall (graph-input-stream.md, "A pose every frame").
struct PoseTimeline
{
    static constexpr size_t cKept = 6;
    static constexpr double cBehindUpdates = 1.5; // in its owner's mean update spacings

    void Add(uint64_t aTick, std::span<const PoseBone> aPose)
    {
        if (aPose.empty())
            return;
        if (!m_poses.empty() && aTick <= m_poses.back().Tick)
            Clear(); // its owner's clock went back
        if (!m_poses.empty())
            m_spacingMs += (std::min(static_cast<double>(aTick - m_poses.back().Tick), 100.0) - m_spacingMs) * 0.05;
        m_poses.push_back({aTick, {aPose.begin(), aPose.end()}});
        if (m_poses.size() > cKept)
            m_poses.pop_front();
    }
    void Clear() noexcept { m_poses.clear(); }

    // The pose to show at aPlayback, within those kept. False with fewer than two.
    bool Shown(uint64_t aPlayback, std::vector<PoseBone>& aOut) const
    {
        if (m_poses.size() < 2)
            return false;
        const double cAt = std::clamp(static_cast<double>(aPlayback) - cBehindUpdates * m_spacingMs,
                                      static_cast<double>(m_poses.front().Tick), static_cast<double>(m_poses.back().Tick));
        size_t next = 1;
        while (static_cast<double>(m_poses[next].Tick) < cAt)
            ++next;
        const auto& cFrom = m_poses[next - 1];
        const auto& cTo = m_poses[next];
        Blend(cFrom.Bones, cTo.Bones, static_cast<float>((cAt - static_cast<double>(cFrom.Tick)) / static_cast<double>(cTo.Tick - cFrom.Tick)), aOut);
        return true;
    }

    // From aFrom (0) to aTo (1), rotations the short way. A bone only one has (a LOD change) is aTo's.
    static void Blend(std::span<const PoseBone> aFrom, std::span<const PoseBone> aTo, float aWeight, std::vector<PoseBone>& aOut)
    {
        aOut.assign(aTo.begin(), aTo.end());
        for (size_t i = 0; i < std::min(aFrom.size(), aTo.size()); ++i)
        {
            const PoseBone& cFrom = aFrom[i];
            PoseBone& out = aOut[i];
            float dot = 0.f, square = 0.f;
            for (int k = 0; k < 4; ++k)
                dot += cFrom.Rotation[k] * out.Rotation[k];
            const float cSign = dot < 0.f ? -1.f : 1.f;
            for (int k = 0; k < 4; ++k)
            {
                out.Translation[k] = std::lerp(cFrom.Translation[k], out.Translation[k], aWeight);
                out.Scale[k] = std::lerp(cFrom.Scale[k], out.Scale[k], aWeight);
                out.Rotation[k] = std::lerp(cFrom.Rotation[k], out.Rotation[k] * cSign, aWeight);
                square += out.Rotation[k] * out.Rotation[k];
            }
            for (float& component : out.Rotation) // never 0: the two are within 90 degrees
                component /= std::sqrt(square);
        }
    }

private:
    struct Kept
    {
        uint64_t Tick = 0;
        std::vector<PoseBone> Bones;
    };
    std::deque<Kept> m_poses;
    double m_spacingMs = 1000.0 / 60.0;
};
