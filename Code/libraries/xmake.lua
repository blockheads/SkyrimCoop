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
    add_deps("TiltedCore", "gamenetworkingsockets")
    add_packages("hopscotch-map", "snappy", "libuv", "spdlog")

    -- Wine MSVC fix: Manually configure protobuf to avoid /external:I
    on_load(function (target)
        import("core.project.project")
        local protobuf = project.required_package("protobuf-cpp")
        if protobuf then
            local includedirs = protobuf:get("sysincludedirs") or protobuf:get("includedirs")
            if includedirs then
                for _, includedir in ipairs(includedirs) do
                    target:add("includedirs", includedir, {force = true})
                end
            end
            local links = protobuf:get("links")
            if links then
                for _, link in ipairs(links) do
                    target:add("links", link)
                end
            end
            local linkdirs = protobuf:get("linkdirs")
            if linkdirs then
                for _, linkdir in ipairs(linkdirs) do
                    target:add("linkdirs", linkdir)
                end
            end
        end
    end)
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end
    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")

    -- Ensure debug symbols in all modes
    if is_plat("windows") then
        set_symbols("debug")
        add_cxflags("/Zi")
        add_ldflags("/DEBUG:FULL")
    elseif is_plat("linux") then
        set_symbols("debug")
        add_cxflags("-g")
    end

-- Reverse Engineering Library (formerly TiltedReverse)
target("SkyrimCoopReverse")
    set_kind("static")
    set_group("Libraries")
    add_files("reverse/*.cpp")
    add_includedirs("reverse/", {public = true})
    add_headerfiles("reverse/*.hpp", "reverse/*.inl")
    add_defines("NOMINMAX")
    add_deps("TiltedCore")
    add_packages("mimalloc", "hopscotch-map", "minhook", "mem", "xbyak")

    -- Ensure debug symbols
    if is_plat("windows") then
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
    add_packages("mimalloc", "hopscotch-map", "mem")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

-- UI Library (formerly TiltedUI) - Client-only, requires CEF
target("SkyrimCoopUI")
    set_kind("static")
    set_group("Libraries")

    -- Disable on Wine MSVC (client-only library)
    if get_config("sdk") == "/opt/msvc" then
        set_enabled(false)
    end

    -- Force MT runtime to match CEF
    if is_mode("releasedbg") or is_mode("release") then
        set_runtimes("MT")
        add_cxflags("/MT", {tools = {"cl"}, force = true})
    end

    add_files("ui/*.cpp")
    add_includedirs("ui/", {public = true})
    add_headerfiles("ui/*.hpp")
    add_syslinks("dxguid", "d3d11")
    add_deps("TiltedCore", "DirectXTK")
    add_packages("cef", "mimalloc", "hopscotch-map")
    add_defines("NOMINMAX")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

    -- Override any inherited runtime settings from dependencies
    after_load(function (target)
        if is_mode("releasedbg") or is_mode("release") then
            target:set("runtimes", "MT")
        end
    end)

-- UI Process Library (CEF render process) - Client-only, requires CEF
target("SkyrimCoopUIProcess")
    set_kind("static")
    set_group("Libraries")

    -- Disable on Wine MSVC (client-only library)
    if get_config("sdk") == "/opt/msvc" then
        set_enabled(false)
    end

    -- CEF is built with static runtime (/MT), so UiProcess must match
    if is_mode("releasedbg") or is_mode("release") then
        set_runtimes("MT")
        add_cxflags("/MT", {tools = {"cl"}, force = true})
    end

    add_files("ui_process/*.cpp")
    add_includedirs("ui_process/", {public = true})
    add_headerfiles("ui_process/*.hpp")
    add_deps("TiltedCore")
    add_packages("cef", "mimalloc", "hopscotch-map")
    add_defines("NOMINMAX")

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end

    -- Override any inherited runtime settings from dependencies
    after_load(function (target)
        if is_mode("releasedbg") or is_mode("release") then
            target:set("runtimes", "MT")
        end
    end)
