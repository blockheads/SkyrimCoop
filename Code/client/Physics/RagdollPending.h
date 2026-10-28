#pragma once

#include <Structs/BodyFrame.h>
#include <map>
#include <vector>

// Frames wait for the copy's next native physics update, before it maps bodies onto bones.
class RagdollPending
{
  public:
    struct Drive
    {
        BodyFrame Frame;
        bool Place{};
    };

    void Put(uint32_t aCopy, const BodyFrame& acFrame, bool aPlace)
    {
        if (m_copies.size() >= 512 && !m_copies.contains(aCopy))
            m_copies.clear();
        auto& drive = m_copies[aCopy][acFrame.Key.Index];
        drive.Frame = acFrame;
        drive.Place |= aPlace;
    }

    std::vector<Drive> Take(uint32_t aCopy)
    {
        std::vector<Drive> out;
        if (auto it = m_copies.find(aCopy); it != m_copies.end())
        {
            for (auto& [index, drive] : it->second)
                out.push_back(std::move(drive));
            m_copies.erase(it);
        }
        return out;
    }

  private:
    std::map<uint32_t, std::map<uint8_t, Drive>> m_copies;
};
