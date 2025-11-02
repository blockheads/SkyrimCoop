#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>

namespace Server
{

struct InventoryComponent
{
    Inventory Content{};
};

} // namespace Server
