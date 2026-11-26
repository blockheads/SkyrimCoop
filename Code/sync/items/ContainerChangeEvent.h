#pragma once

/**
 * @brief A reference's count of an item changed in this game (raised for containers and actors alike).
 */
struct ContainerChangeEvent
{
    uint32_t ContainerId{};
    uint32_t ItemId{};
};
