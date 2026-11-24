#pragma once

#include <Physics/BodyPrediction.h>

#include <map>
#include <vector>

// Marks each awake body falling freely or not, from its velocity's change since the last read (shared by every
// guest's sends).
class BodyMotions
{
public:
    void Mark(std::vector<BodyFrame>& aAwake, uint64_t aTick) noexcept;
    void Forget(const BodyKey& acKey) noexcept;

private:
    struct Last
    {
        glm::vec3 Velocity{};
        uint64_t Tick{};
        bool Gravity{};
    };
    std::map<BodyKey, Last> m_last;
};
