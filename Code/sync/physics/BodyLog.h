#pragma once

#include <Structs/BodyFrame.h>

#include <format>
#include <string>

// A physics body as a log line names it.
namespace BodyLog
{
// Its reference's id (or actor's session id) and its index there.
inline std::string Describe(const BodyKey& acKey)
{
    return acKey.Actor ? std::format("actor {:X} body {}", acKey.Actor, acKey.Index) : std::format("{:X} body {}", acKey.Object.LogFormat(), acKey.Index);
}
} // namespace BodyLog
