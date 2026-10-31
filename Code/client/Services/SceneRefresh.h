#pragma once

#include <Structs/ActorAiState.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace SceneRefresh
{
using States = std::unordered_map<uint32_t, ActorAiState>;
template <class Read>
std::vector<std::pair<uint32_t, ActorAiState>> ReadChanges(const States& acSent, Read&& aRead)
{
    std::vector<std::pair<uint32_t, ActorAiState>> changes;
    for (const auto& [id, state] : acSent)
        if (state.Scene)
            if (const auto fresh = aRead(id); fresh && *fresh != state)
                changes.emplace_back(id, *fresh);
    return changes;
}
}
