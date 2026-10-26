#include <Physics/BodyRelevance.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <iterator>

namespace BodyRelevance
{
std::vector<BodyFrame> Near(const std::vector<BodyFrame>& acAwake, const std::optional<glm::vec3>& acGuest) noexcept
{
    if (!acGuest)
        return acAwake;
    std::vector<BodyFrame> kept;
    std::ranges::copy_if(acAwake, std::back_inserter(kept),
                         [&](const BodyFrame& acFrame) { return glm::distance(acFrame.Position, *acGuest) <= kGuestRadius; });
    return kept;
}
} // namespace BodyRelevance
