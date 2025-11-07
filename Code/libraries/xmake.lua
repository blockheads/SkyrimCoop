-- Merged Tilted Libraries
-- This file consolidates TiltedConnect, TiltedHooks, TiltedReverse, and TiltedUI
-- into the main SkyrimCoop repository for easier debugging and maintenance

set_languages("cxx20")

-- Networking Library (formerly TiltedConnect)
target("SkyrimCoopNetworking")
    set_kind("static")
    set_group("Libraries")
    add_files("networking/*.cpp")
    add_includedirs("networking/", {public = true})
    add_headerfiles("networking/*.hpp")
    add_deps("TiltedCore")
    add_packages("hopscotch-map", "enet6", "libuv", "spdlog", "snappy")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

    -- Debug symbols (reduced in debug mode for faster builds)
    if is_plat("windows") then
        if is_mode("debug") then
            set_symbols("debug")
            add_cxflags("/Zi")
            add_ldflags("/DEBUG:FASTLINK")  -- Much faster than /DEBUG:FULL
        else
            set_symbols("debug")
            add_cxflags("/Zi")
            add_ldflags("/DEBUG:FULL")
        end
    elseif is_plat("linux") or is_plat("mingw") then
        if is_mode("debug") then
            set_symbols("debug")
            -- Already using -g1 from root xmake.lua for MinGW
        else
            set_symbols("debug")
            add_cxflags("-g")
        end
    end

-- Windows-only libraries (Client components) - also built for MinGW cross-compile
if is_plat("windows") or is_plat("mingw") then
    -- Reverse Engineering Library (formerly TiltedReverse)
    target("SkyrimCoopReverse")
        set_kind("static")
        set_group("Libraries")
        add_files("reverse/*.cpp")
        add_includedirs("reverse/", {public = true})
        add_includedirs("$(projectdir)/Code/external", {public = true})
        add_headerfiles("reverse/*.hpp", "reverse/*.inl")
        add_defines("NOMINMAX")
        add_deps("TiltedCore")
        add_packages("rpmalloc", "minhook", "hopscotch-map", "xbyak")
        if not is_mode("debug") then
            set_symbols("debug")
        end

    -- Hooks Library (formerly TiltedHooks)
    target("SkyrimCoopHooks")
        set_kind("static")
        set_group("Libraries")
        add_files("hooks/*.cpp")
        add_files("hooks/DInputHook.cpp", {unity_ignored = true})
        add_includedirs("hooks/", {public = true})
        add_headerfiles("hooks/*.hpp")
        add_syslinks("dxguid", "dinput8", "d3d11")
        add_deps("TiltedCore", "SkyrimCoopReverse")
        add_packages("rpmalloc", "hopscotch-map")
        if not is_mode("debug") then
            set_symbols("debug")
        end

    -- UI Library (formerly TiltedUI)
    target("SkyrimCoopUI")
        set_kind("static")
        set_group("Libraries")

        -- Force MT runtime to match CEF
        if is_mode("releasedbg") or is_mode("release") then
            set_runtimes("MT")
            add_cxflags("/MT", {tools = {"cl"}, force = true})
        end

        add_files("ui/*.cpp")
        add_includedirs("ui/", {public = true})
        add_headerfiles("ui/*.hpp")
        add_syslinks("dxguid", "d3d11")
        add_deps("TiltedCore")
        add_packages("cef", "directxtk", "rpmalloc", "hopscotch-map")
        add_defines("NOMINMAX")
        if not is_mode("debug") then
            set_symbols("debug")
        end

        -- Override any inherited runtime settings from dependencies
        after_load(function (target)
            if is_mode("releasedbg") or is_mode("release") then
                target:set("runtimes", "MT")
            end
        end)

    -- UI Process Library (CEF render process)
    target("SkyrimCoopUIProcess")
        set_kind("static")
        set_group("Libraries")

        -- CEF is built with static runtime (/MT), so UiProcess must match
        if is_mode("releasedbg") or is_mode("release") then
            set_runtimes("MT")
            add_cxflags("/MT", {tools = {"cl"}, force = true})
        end

        add_files("ui_process/*.cpp")
        add_includedirs("ui_process/", {public = true})
        add_headerfiles("ui_process/*.hpp")
        add_deps("TiltedCore")
        add_packages("cef", "rpmalloc", "hopscotch-map")
        add_defines("NOMINMAX")
        if not is_mode("debug") then
            set_symbols("debug")
        end

        -- Override any inherited runtime settings from dependencies
        after_load(function (target)
            if is_mode("releasedbg") or is_mode("release") then
                target:set("runtimes", "MT")
            end
        end)
end
