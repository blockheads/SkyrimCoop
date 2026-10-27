#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

// PlayerCharacter::ProcessQueuedTravel (40438) sees this destination before beginning its load.
struct PlayerTravelQueuedEvent
{
    uint32_t WorldSpaceId{};
    uint32_t CellId{};
    glm::vec3 Position{};
    float Heading{};
};
