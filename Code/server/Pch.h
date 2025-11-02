#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <TiltedCore/Outcome.hpp>
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <cstdint>

#include <any>
#include <chrono>
#include <codecvt>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>

#include <Server.hpp>
#include <cxxopts.hpp>

#include <BuildInfo.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <ConsoleRegistry.h>
#include <StringCache.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <sol/sol.hpp>

using namespace std::chrono_literals;

using TiltedPhoques::ConnectionId_t;
using TiltedPhoques::MakeShared;
using TiltedPhoques::MakeUnique;
using TiltedPhoques::Map;
using TiltedPhoques::ScopedAllocator;
using TiltedPhoques::String;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;
using TiltedPhoques::ViewBuffer;

#undef GetClassName

#include <Components.h>
