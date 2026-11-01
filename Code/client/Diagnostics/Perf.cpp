#include "Perf.h"

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace Diagnostics
{
namespace
{
// The nearest-rank percentile of sorted values.
float Percentile(const std::vector<float>& acSorted, double aFraction) noexcept
{
    if (acSorted.empty())
        return 0.f;
    const auto cRank = static_cast<size_t>(aFraction * static_cast<double>(acSorted.size()) + 0.999999);
    return acSorted[std::clamp<size_t>(cRank, 1, acSorted.size()) - 1];
}
} // namespace

FrameTimes::FrameTimes(size_t aCapacity)
    : m_samples(aCapacity)
{
}

void FrameTimes::Add(int64_t aTimeMs, float aFrameMs, float aUpdateMs) noexcept
{
    m_samples[m_next] = {aTimeMs, aFrameMs, aUpdateMs};
    m_next = (m_next + 1) % m_samples.size();
    m_count = std::min(m_count + 1, m_samples.size());
}

FrameTimes::Summary FrameTimes::Last(int64_t aWindowMs) const
{
    Summary summary;
    if (!m_count)
        return summary;
    const int64_t cNewest = m_samples[(m_next + m_samples.size() - 1) % m_samples.size()].TimeMs;
    std::vector<float> frames, updates;
    int64_t oldest = cNewest;
    for (size_t i = 0; i < m_count; ++i)
    {
        const Sample& cSample = m_samples[(m_next + m_samples.size() - 1 - i) % m_samples.size()];
        if (cNewest - cSample.TimeMs > aWindowMs)
            break;
        frames.push_back(cSample.FrameMs);
        updates.push_back(cSample.UpdateMs);
        oldest = cSample.TimeMs;
    }
    std::sort(frames.begin(), frames.end());
    std::sort(updates.begin(), updates.end());
    summary.Frames = frames.size();
    summary.Seconds = static_cast<double>(cNewest - oldest) / 1000.0;
    summary.FrameP50 = Percentile(frames, 0.50);
    summary.FrameP95 = Percentile(frames, 0.95);
    summary.FrameP99 = Percentile(frames, 0.99);
    summary.FrameMax = frames.back();
    summary.UpdateP50 = Percentile(updates, 0.50);
    summary.UpdateP95 = Percentile(updates, 0.95);
    summary.UpdateMax = updates.back();
    return summary;
}

FrameTimes& Frames() noexcept
{
    static FrameTimes s_frames(16384); // a minute at well over 200 fps
    return s_frames;
}

std::vector<TrafficRow> Traffic(const std::vector<MessageEntry>& acEntries, int64_t aSinceMs)
{
    // By the name's text: two literals of one name may differ in address.
    std::map<std::pair<std::string_view, char>, TrafficRow> rows;
    for (const auto& cEntry : acEntries)
    {
        if (cEntry.TimeMs < aSinceMs)
            continue;
        const char* pName = cEntry.Name ? cEntry.Name : "?";
        auto [it, _] = rows.try_emplace({pName, static_cast<char>(cEntry.Way)}, TrafficRow{pName, cEntry.Way, 0, 0});
        ++it->second.Count;
        it->second.Bytes += cEntry.Size;
    }
    std::vector<TrafficRow> result;
    for (const auto& [_, cRow] : rows)
        result.push_back(cRow);
    std::sort(result.begin(), result.end(), [](const TrafficRow& a, const TrafficRow& b) { return a.Bytes > b.Bytes; });
    return result;
}
} // namespace Diagnostics
