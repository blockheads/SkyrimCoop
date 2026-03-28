-- Relay DLL: Minimal MinGW-compiled SKSE plugin that acts as a TCP relay
-- Per D-13: Only MinHook + winsock2, no EnTT/TiltedCore/spdlog/encoding
-- Per D-10: Still an SKSE plugin (exports SKSEPlugin_Version, SKSEPlugin_Load)
-- Per D-14: No game struct headers -- DLL forwards raw pointer values only

if is_plat("mingw") then
    target("SkyrimCoopHooksDLL")
        set_kind("shared")
        set_group("RelayDLL")
        set_basename("skyrim_coop_hooks")
        set_prefixname("")  -- no "lib" prefix on MinGW

        -- All .cpp files in relay_dll/ (new files picked up automatically)
        add_files("*.cpp")
        add_includedirs(".")

        -- Only MinHook for function hooking (per D-13)
        add_packages("minhook")

        -- System libraries: winsock2 for TCP, kernel32 for process management
        add_syslinks("ws2_32", "kernel32")

        -- Force-include MinGWCompat.h for MSVC calling convention compat
        add_cxflags("-include MinGWCompat.h", {force = true})
        add_includedirs("../client")  -- for MinGWCompat.h

        -- Static link MinGW runtime (no libgcc/libstdc++/libwinpthread DLL deps)
        add_shflags(
            "-static-libgcc",
            "-static-libstdc++",
            "-Wl,-Bstatic", "-lstdc++", "-lpthread",
            "-Wl,-Bdynamic",
            {force = true})

    target_end()
end
