#include "MovementLoader.h"
#include "MovementMath.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (0)

static bool Loaded(const std::filesystem::path& path)
{
#ifdef _WIN32
    return GetModuleHandleW(path.c_str()) != nullptr;
#else
    auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle) dlclose(handle);
    return handle != nullptr;
#endif
}

int main(int argc, char** argv)
{
    CHECK(argc == 6);
    const auto baseline = std::filesystem::absolute(argv[1]);
    const auto variant = std::filesystem::absolute(argv[2]);
    CoopReload::MovementLoader loader;
    CoopMovementInput input{};
    input.first_tick = 100;
    input.second_tick = 200;
    input.tick = 150;
    input.second_position[0] = 20;
    CoopMovementOutput output{};
    CHECK(!loader.Evaluate(input, output));
    CHECK(output.position[0] == 10.f);
    input.tick = 0; // Clock startup must not underflow to the newest position.
    loader.Evaluate(input, output);
    CHECK(output.position[0] == 0.f);
    input.tick = 150;

    std::string error;
    CHECK(loader.Reload(baseline, error));
    CHECK(loader.Evaluate(input, output));
    CHECK(output.position[0] == 10.f);
    CHECK(loader.Generation() == 1);
    CHECK(!loader.Reload(baseline, error));
    CHECK(loader.Reload(variant, error));
    CHECK(!Loaded(baseline)); // Real dlclose/FreeLibrary, not a fake loader.
    CHECK(loader.Evaluate(input, output));
    CHECK(output.position[0] == 110.f);

    CHECK(!loader.Reload(std::filesystem::absolute(argv[3]), error)); // wrong ABI
    CHECK(!loader.Reload(std::filesystem::absolute(argv[4]), error)); // invalid output
    CHECK(!loader.Reload(variant.parent_path() / "missing.dll", error));
    CHECK(loader.Generation() == 2);
    CHECK(loader.Evaluate(input, output));
    CHECK(output.position[0] == 110.f); // Failed replacements retain old code.

    std::atomic<bool> stop{false};
    std::atomic<unsigned> calls{0};
    std::thread caller([&] {
        while (!stop)
        {
            CoopMovementOutput result{};
            CHECK(loader.Evaluate(input, result));
            CHECK(result.position[0] == 10.f || result.position[0] == 110.f);
            ++calls;
        }
    });
    while (calls < 100) std::this_thread::yield();
    for (int i = 0; i < 50; ++i)
    {
        CHECK(loader.Reload(baseline, error));
        CHECK(!Loaded(variant));
        CHECK(loader.Reload(variant, error));
        CHECK(!Loaded(baseline));
    }
    stop = true;
    caller.join();
    CHECK(calls >= 100);
    CHECK(loader.Generation() == 102);

    CHECK(loader.Reload(std::filesystem::absolute(argv[5]), error));
    input.tick = 42;
    CHECK(!loader.Evaluate(input, output)); // Runtime bad output uses fallback.
    CHECK(output.position[0] == 0.f);
    CHECK(loader.InvalidOutputs() == 1);
    loader.Unload();
    CHECK(!Loaded(std::filesystem::absolute(argv[5])));
    CHECK(loader.BuildId() == "builtin");
    CHECK(!loader.Evaluate(input, output));
    std::cout << "PASS: 103 live loads, real unloads, concurrent calls, ABI rejection, "
                 "failed-load recovery and invalid-output fallback. No Skyrim was used.\n";
}
