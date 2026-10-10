#include "MovementApi.h"
#include "MovementMath.h"
#include <cstring>
#include <limits>

#ifndef TEST_VARIANT
#define TEST_VARIANT 1
#endif

static void Evaluate(const CoopMovementInput* input, CoopMovementOutput* output)
{
    CoopReload::DefaultMovement(*input, *output);
    output->position[0] += 100.f;
    if (TEST_VARIANT == 3 || (TEST_VARIANT == 4 && input->tick == 42))
        output->position[0] = std::numeric_limits<float>::quiet_NaN();
}

#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C" __attribute__((visibility("default")))
#endif
int Coop_GetMovementApi(uint32_t, uint32_t, CoopMovementApi* output)
{
    *output = {};
    output->version = TEST_VARIANT == 2 ? 999 : COOP_MOVEMENT_ABI;
    output->size = sizeof(*output);
    std::strcpy(output->build_id, "test-variant");
    output->evaluate = &Evaluate;
    return 1;
}
