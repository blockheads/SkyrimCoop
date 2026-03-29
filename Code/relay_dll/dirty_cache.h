#pragma once
#include <unordered_map>
#include <cstdint>
#include <cmath>

struct PositionState {
    float x, y, z, rx, ry, rz;
};

class DirtyCache
{
public:
    static constexpr float kPositionEpsilon = 1.0f;
    static constexpr float kRotationEpsilon = 0.01f;

    // Returns true if position/rotation changed beyond epsilon (should send).
    // First call for a given formId always returns true.
    bool CheckPosition(uint32_t aFormId, float aX, float aY, float aZ,
                       float aRx, float aRy, float aRz)
    {
        auto it = m_positions.find(aFormId);
        if (it == m_positions.end())
        {
            m_positions[aFormId] = {aX, aY, aZ, aRx, aRy, aRz};
            return true;
        }

        auto& s = it->second;
        bool dirty = std::fabs(aX - s.x) > kPositionEpsilon ||
                     std::fabs(aY - s.y) > kPositionEpsilon ||
                     std::fabs(aZ - s.z) > kPositionEpsilon ||
                     std::fabs(aRx - s.rx) > kRotationEpsilon ||
                     std::fabs(aRy - s.ry) > kRotationEpsilon ||
                     std::fabs(aRz - s.rz) > kRotationEpsilon;

        if (dirty)
        {
            s = {aX, aY, aZ, aRx, aRy, aRz};
        }
        return dirty;
    }

    // Returns true if delta changed (exact compare — discrete game-tick values).
    // First call for a given formId always returns true.
    bool CheckActorProcess(uint32_t aFormId, float aDelta)
    {
        auto it = m_actorProcess.find(aFormId);
        if (it == m_actorProcess.end())
        {
            m_actorProcess[aFormId] = aDelta;
            return true;
        }

        if (it->second != aDelta)
        {
            it->second = aDelta;
            return true;
        }
        return false;
    }

    void Remove(uint32_t aFormId)
    {
        m_positions.erase(aFormId);
        m_actorProcess.erase(aFormId);
    }

    void Clear()
    {
        m_positions.clear();
        m_actorProcess.clear();
    }

private:
    std::unordered_map<uint32_t, PositionState> m_positions;
    std::unordered_map<uint32_t, float> m_actorProcess;
};
