#include "MovementLoader.h"
#include "MovementMath.h"
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace CoopReload
{
namespace
{
void CloseLibrary(void* handle)
{
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}
}

MovementLoader::~MovementLoader() { Unload(); }

bool MovementLoader::Reload(const std::filesystem::path& path, std::string& error)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    // Every publication uses a fresh filename, even for an identical build.
    // Loading the same name can merely increment the old DLL's reference count.
    if (path == m_path && m_library)
    {
        error = "publish to a new filename before reloading";
        return false;
    }
#ifdef _WIN32
    void* candidate = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!candidate)
    {
        error = "LoadLibraryEx failed: " + std::to_string(GetLastError());
        return false;
    }
    auto getApi = reinterpret_cast<CoopGetMovementApi>(GetProcAddress(static_cast<HMODULE>(candidate), "Coop_GetMovementApi"));
#else
    void* candidate = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!candidate)
    {
        error = dlerror();
        return false;
    }
    auto getApi = reinterpret_cast<CoopGetMovementApi>(dlsym(candidate, "Coop_GetMovementApi"));
#endif
    CoopMovementApi api{};
    if (!getApi || !getApi(COOP_MOVEMENT_ABI, sizeof(api), &api) ||
        api.version != COOP_MOVEMENT_ABI || api.size != sizeof(api) ||
        !api.evaluate || !api.build_id[0] || !std::memchr(api.build_id, 0, sizeof(api.build_id)))
    {
        CloseLibrary(candidate);
        error = "missing export or incompatible movement ABI";
        return false;
    }

    // Reject a module that does not initialize its output or returns NaN/Inf.
    CoopMovementInput probe{};
    probe.second_tick = 100;
    CoopMovementOutput result;
    for (int i = 0; i < 3; ++i)
        result.position[i] = result.rotation[i] = std::numeric_limits<float>::quiet_NaN();
    api.evaluate(&probe, &result);
    if (!ValidMovement(result))
    {
        CloseLibrary(candidate);
        error = "movement module returned invalid output during validation";
        return false;
    }

    void* previous = m_library;
    m_library = candidate;
    m_api = api;
    m_path = path;
    ++m_generation;
    m_calls = 0;
    m_invalidOutputs = 0;
    CloseLibrary(previous);
    error.clear();
    return true;
}

void MovementLoader::Unload()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    auto previous = m_library;
    m_library = nullptr;
    m_api = {};
    m_path.clear();
    CloseLibrary(previous);
}

bool MovementLoader::Evaluate(const CoopMovementInput& input, CoopMovementOutput& output)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_library)
    {
        for (int i = 0; i < 3; ++i)
            output.position[i] = output.rotation[i] = std::numeric_limits<float>::quiet_NaN();
        m_api.evaluate(&input, &output);
        ++m_calls;
        if (ValidMovement(output)) return true;
        ++m_invalidOutputs;
    }
    DefaultMovement(input, output);
    return false;
}

std::string MovementLoader::BuildId() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_library ? m_api.build_id : "builtin";
}
uint64_t MovementLoader::Generation() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_generation;
}
uint64_t MovementLoader::Calls() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_calls;
}
uint64_t MovementLoader::InvalidOutputs() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_invalidOutputs;
}
}
