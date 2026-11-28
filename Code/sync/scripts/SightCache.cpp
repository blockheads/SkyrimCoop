#include <scripts/SightCache.h>

SightCache::Reading SightCache::Sees(uint32_t aGuest, uint32_t aTarget, Clock::time_point aNow) noexcept
{
    std::scoped_lock _{m_lock};
    Entry& entry = m_entries[{aGuest, aTarget}];
    Reading reading{.Sees = entry.Sees && entry.AnsweredAt && aNow - *entry.AnsweredAt < kFresh};
    if (entry.AskedAt && aNow - *entry.AskedAt < kLost)
        return reading;
    const uint32_t cReplyId = kReplyBits | (m_next++ & ~kReplyBits);
    std::erase_if(m_asked, [&](const auto& acAsked) { return acAsked.second == std::pair{aGuest, aTarget}; });
    m_asked[cReplyId] = {aGuest, aTarget};
    entry.AskedAt = aNow;
    reading.Ask = Question{cReplyId, aGuest, aTarget};
    return reading;
}

bool SightCache::Answer(uint32_t aReplyId, bool aSees, Clock::time_point aNow) noexcept
{
    if (!IsSightReply(aReplyId))
        return false;
    std::scoped_lock _{m_lock};
    const auto cAsked = m_asked.extract(aReplyId);
    if (!cAsked)
        return true; // asked again since: the newer question's answer counts
    Entry& entry = m_entries[cAsked.mapped()];
    entry.Sees = aSees;
    entry.AnsweredAt = aNow;
    entry.AskedAt.reset();
    return true;
}
