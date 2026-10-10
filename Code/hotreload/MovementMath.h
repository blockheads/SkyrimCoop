#pragma once

#include "MovementApi.h"
#include <algorithm>
#include <cmath>

namespace CoopReload
{
inline void DefaultMovement(const CoopMovementInput& input, CoopMovementOutput& output)
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float circle = 2.f * pi;
    float fraction = 0.f;
    if (input.tick >= input.second_tick)
        fraction = 1.f;
    else if (input.tick > input.first_tick && input.second_tick > input.first_tick)
        fraction = static_cast<float>(static_cast<double>(input.tick - input.first_tick) /
                                      static_cast<double>(input.second_tick - input.first_tick));

    for (int axis = 0; axis < 3; ++axis)
    {
        output.position[axis] = input.first_position[axis] +
            (input.second_position[axis] - input.first_position[axis]) * fraction;
        float difference = std::fmod(input.second_rotation[axis] - input.first_rotation[axis], circle);
        if (difference > pi) difference -= circle;
        if (difference < -pi) difference += circle;
        float angle = std::fmod(input.first_rotation[axis] + difference * fraction, circle);
        if (angle < 0.f) angle += circle;
        if (axis == 0 && angle > pi / 2.f) angle -= circle;
        output.rotation[axis] = angle;
    }
}

inline bool ValidMovement(const CoopMovementOutput& output)
{
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(output.position[axis]) || !std::isfinite(output.rotation[axis]))
            return false;
    return true;
}
}
