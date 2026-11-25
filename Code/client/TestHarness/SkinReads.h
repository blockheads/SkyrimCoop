#pragma once

#include <nlohmann/json.hpp>

namespace TestHarness
{
// An actor's skinned geometry as it is drawn (NiSkinInstance::UpdateBoneMatrices 77461): which skeleton each skin
// follows, the frame its matrices were built, and hashes of the matrices and of the nodes they should follow.
nlohmann::json ReadSkins(uint32_t aFormId);
} // namespace TestHarness
