-- Native client: Linux ELF binary with full ECS, services, and embedded server
-- Per D-12: Builds natively on Linux with system GCC/Clang
-- Per D-14: Includes game struct headers for /proc/pid/mem reads

if is_plat("linux") then
    target("SkyrimCoopNative")
        set_kind("binary")
        set_group("NativeClient")
        set_basename("skyrim-coop")

        -- Source files
        add_files("**.cpp")

        -- Include dirs
        add_includedirs(".")
        add_includedirs("../relay_dll")         -- shared protocol.h
        add_includedirs("../client/Games/Skyrim")  -- game struct headers (per D-14)

        -- Dependencies: encoding + embedded server for host mode
        add_deps("SkyrimEncoding")
        add_deps("SkyrimTogetherServer")

        -- Packages: full native stack
        add_packages(
            "entt",
            "spdlog",
            "glm",
            "rpmalloc",
            "hopscotch-map",
            "cryptopp",
            "enet6")

        -- System libraries
        add_syslinks("pthread")

        -- Match MSVC struct layout for game struct headers (per Pitfall 6)
        add_cxflags("-mms-bitfields", {force = true})

        -- C++20
        set_languages("cxx20")

    target_end()
end
