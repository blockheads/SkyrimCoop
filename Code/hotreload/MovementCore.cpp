#include "MovementApi.h"
#include "MovementMath.h"
#include <cstring>

#ifndef COOP_BUILD_ID
#define COOP_BUILD_ID "development"
#endif

namespace
{
// This is the code to edit/rebuild while Skyrim stays open. Keep this module
// stateless: the resident plugin owns actors, interpolation history and sockets.
void Evaluate(const CoopMovementInput* input, CoopMovementOutput* output)
{
    CoopReload::DefaultMovement(*input, *output);
}
}

#ifdef _WIN32
#define COOP_EXPORT extern "C" __declspec(dllexport)
#else
#define COOP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

COOP_EXPORT int Coop_GetMovementApi(uint32_t version, uint32_t size, CoopMovementApi* output)
{
    if (!output || version != COOP_MOVEMENT_ABI || size != sizeof(CoopMovementApi))
        return 0;
    *output = {};
    output->version = COOP_MOVEMENT_ABI;
    output->size = sizeof(CoopMovementApi);
    static_assert(sizeof(COOP_BUILD_ID) <= sizeof(output->build_id));
    std::memcpy(output->build_id, COOP_BUILD_ID, sizeof(COOP_BUILD_ID));
    output->evaluate = &Evaluate;
    return 1;
}
