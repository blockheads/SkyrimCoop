#pragma once

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>
#include <BSAnimationGraphManager.h>

struct BehaviorVar
{
    struct Replacer
    {
        uint64_t origHash;
        uint64_t newHash;
        std::string signatureVar;
        std::string creatureName;
        std::vector<std::string> syncBooleanVar;
        std::vector<std::string> syncFloatVar;
        std::vector<std::string> syncIntegerVar;
        bool operator==(const uint64_t& acRHS) const { return origHash == acRHS; }
    };

    static BehaviorVar* Get();
    const AnimationGraphDescriptor* Patch(BSAnimationGraphManager* apManager, Actor* apActor);
    bool FailListed(const uint64_t acHash);
    void FailList(const uint64_t acHash);
    static const bool IsDragon(const uint64_t acHash) { return acHash == Get()->m_dragonGraphDescriptorHash; }
    static const uint64_t GetHumanoidHash()           { return Get()->m_humanoidGraphDescriptorHash; }

    void Init();
    void Debug();

private:
    static BehaviorVar* single;
    uint64_t invocations = 0;

    // These start out with the unmodded hashes and save the modded
    // hash if behavior is modified. Humanoid is used to return from 
    // beast mode, and dragon is used for IsDragon() test.
    uint64_t m_humanoidGraphDescriptorHash {AnimationGraphDescriptor_Master_Behavior::m_key};
    uint64_t m_dragonGraphDescriptorHash   {AnimationGraphDescriptor_BHR_Master::m_key};

    void SeedAnimationVariables(
        const uint64_t acHash,
        const AnimationGraphDescriptor* acpDescriptor,
        std::unordered_map<std::string, uint32_t>& acReverseMap,
        std::unordered_set<uint32_t>& aBoolVars,
        std::unordered_set<uint32_t>& aFloatVars,
        std::unordered_set<uint32_t>& aIntVars);

    const std::vector<std::string> TokenizeBehaviorSig(const std::string& acSignature) const;
    const AnimationGraphDescriptor* ConstructModdedDescriptor(
        const uint64_t acNewHash,
        const Replacer& acReplacer,
        std::unordered_map<std::string, uint32_t>& acReverseMap);

    std::vector<std::filesystem::path> LoadDirs(const std::filesystem::path& acPath);
    Replacer* LoadReplacerFromDir(const std::filesystem::path& acDir);
    std::vector<uint64_t> SignatureMatches(const uint64_t acHash, const std::string& acSignature) const;

    std::vector<Replacer> behaviorPool; // Pool for loaded behaviors
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> failedBehaviors;
};

extern const AnimationGraphDescriptor* BehaviorVarPatch(BSAnimationGraphManager* apManager, Actor* apActor);
