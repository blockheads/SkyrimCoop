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
        add_includedirs("../relay_dll")         -- shared protocol.h + json_protocol.h
        add_includedirs("third_party")          -- nlohmann/json.hpp + json_fwd.hpp
        add_includedirs("../client/Games/Skyrim")  -- game struct headers (per D-14)

        -- Dependencies: encoding + embedded server for host mode
        add_deps("SkyrimEncoding")
        add_deps("SkyrimTogetherServer")

        -- Include dirs for server headers (GameServer.h -> World.h chain)
        add_includedirs("../server")
        add_includedirs("../common")
        add_includedirs("../components")
        add_includedirs("../base")
        add_includedirs("../admin_protocol")
        add_includedirs("../libraries/networking")
        add_includedirs("../external")

        -- Packages: full native stack + server transitive deps (sol2 via ScriptService.inl)
        add_packages(
            "entt",
            "spdlog",
            "glm",
            "rpmalloc",
            "hopscotch-map",
            "cryptopp",
            "enet6",
            "lua",
            "sol2",
            "sqlite3")

        -- System libraries
        add_syslinks("pthread")

        -- Match MSVC struct layout for game struct headers (per Pitfall 6)
        add_cxflags("-mms-bitfields", {force = true})

        -- C++20
        set_languages("cxx20")

        -- Auto-deploy to Skyrim SKSE plugins dir after build
        after_build(function (target)
            local skse_dir = "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins"
            if os.isdir(skse_dir) then
                os.cp(target:targetfile(), path.join(skse_dir, "skyrim-coop"))
                print("Deployed skyrim-coop -> " .. skse_dir)
            end
        end)

    target_end()
end
