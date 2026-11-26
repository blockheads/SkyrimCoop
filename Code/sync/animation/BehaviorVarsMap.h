#pragma once

struct BehaviorVars
{
    uint64_t m_key;
    std::unordered_map<std::string, uint32_t> m_nameMap;
    std::unordered_map<uint32_t, std::string> m_valueMap;
};

class BehaviorVarsMap
{
private:
    std::unordered_map<uint64_t, BehaviorVars> m_map;

    // Singleton
    BehaviorVarsMap(){};
    BehaviorVarsMap(BehaviorVarsMap const&) = delete;
    void operator=(BehaviorVarsMap const&) = delete;

public:
    const uint32_t find(const uint64_t acBehaviorVarKey, const std::string acName);

    const std::string find(const uint64_t acBehaviorVarKey, const uint32_t acValue);
    void Hashes(std::vector<uint64_t>& aHashes) const
    {
        aHashes.clear();
        for (auto& item : m_map)
            aHashes.push_back(item.first);
    };

    void Register(const BehaviorVars aMap);

    static BehaviorVarsMap& getInstance();
};

namespace BehaviorOrig
{
extern void BehaviorOrigInit();
};
