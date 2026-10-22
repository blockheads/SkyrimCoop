#pragma once

// What the host's code (Code/client/Services/Host/) takes from the client's precompiled header.
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

#include <BuildVersion.h>
#include <ENetInterface.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

using namespace std::chrono_literals;

using TiltedPhoques::ConnectionId_t;
using TiltedPhoques::MakeUnique;
using TiltedPhoques::Map;
using TiltedPhoques::ScopedAllocator;
using TiltedPhoques::String;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;
using TiltedPhoques::ViewBuffer;
