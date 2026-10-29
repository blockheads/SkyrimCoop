#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/hkbBehaviorGraph.h>

#include <Games/Crc64.h>

std::map<uint32_t, std::string> BSAnimationGraphManager::DumpAnimationVariables(bool aPrintVariables)
{
    std::map<uint32_t, std::string> variables;

    if (animationGraphIndex < animationGraphs.size)
    {
        const auto pGraph = animationGraphs.Get(animationGraphIndex);
        if (pGraph && pGraph->hkxDB && pGraph->behaviorGraph)
        {
            const auto pDb = pGraph->hkxDB;
            const auto pBuckets = pDb->animationVariables.buckets;
            const auto pVariableSet = pGraph->behaviorGraph->animationVariables;

            if (pBuckets && pVariableSet)
            {
                for (decltype(pDb->animationVariables.bucketCount) i = 0; i < pDb->animationVariables.bucketCount; ++i)
                {
                    auto pBucket = &pBuckets[i];
                    if (!pBucket->next)
                        continue;

                    while (pBucket != pDb->animationVariables.end)
                    {
                        const auto variableIndex = pBucket->value;
                        if (pVariableSet->size > static_cast<uint32_t>(variableIndex))
                        {
                            variables[variableIndex] = pBucket->key.AsAscii();
                        }

                        pBucket = pBucket->next;
                    }
                }

                if (aPrintVariables)
                {
                    for (auto& [id, name] : variables)
                    {
                        std::cout << "k" << name << " = " << id << "," << std::endl;
                    }
                }
            }
        }
    }

    return variables;
}

BShkbAnimationGraph* BSAnimationGraphManager::GetGraph(int aForceIndex) noexcept
{
    const auto cIndex = aForceIndex == -1 ? animationGraphIndex : static_cast<uint32_t>(aForceIndex);
    return cIndex < animationGraphs.size ? animationGraphs.Get(cIndex) : nullptr;
}

uint64_t BSAnimationGraphManager::GetProjectKey(int aForceIndex) noexcept
{
    const auto* pGraph = GetGraph(aForceIndex);
    const auto* pName = pGraph ? pGraph->projectName : nullptr;
    return pName ? Crc64(reinterpret_cast<const unsigned char*>(pName), std::strlen(pName)) : 0;
}

uint64_t BSAnimationGraphManager::GetDescriptorKey(int aForceIndex)
{
    std::string variableNames{};
    variableNames.reserve(8192);
    std::map<uint32_t, const char*> variables;

    if (const auto pGraph = GetGraph(aForceIndex))
    {
        if (pGraph->hkxDB && pGraph->behaviorGraph)
        {
            const auto pDb = pGraph->hkxDB;
            const auto pBuckets = pDb->animationVariables.buckets;
            const auto pVariableSet = pGraph->behaviorGraph->animationVariables;

            if (pBuckets && pVariableSet)
            {
                for (decltype(pDb->animationVariables.bucketCount) i = 0; i < pDb->animationVariables.bucketCount; ++i)
                {
                    auto pBucket = &pBuckets[i];
                    if (!pBucket->next)
                        continue;

                    while (pBucket != pDb->animationVariables.end)
                    {
                        const auto variableIndex = pBucket->value;
                        if (pVariableSet->size > static_cast<uint32_t>(variableIndex))
                        {
                            variables[variableIndex] = pBucket->key.AsAscii();
                        }

                        pBucket = pBucket->next;
                    }
                }

                for (auto& [id, name] : variables)
                {
                    variableNames += name;
                }
            }
        }
    }

    std::transform(variableNames.begin(), variableNames.end(), variableNames.begin(), [](unsigned char c) { return std::tolower(c); });

    return Crc64(reinterpret_cast<const unsigned char*>(variableNames.c_str()), variableNames.size());
}
