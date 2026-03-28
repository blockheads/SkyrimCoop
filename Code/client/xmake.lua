
local function build_client(name)
target(name)
    set_kind("static")
    set_group("Client")
    add_includedirs(".","../external/")
    -- Add server include directory for embedded GameServer (P2P hosting)
    add_includedirs("../server", {public = false})
    if is_plat("mingw") then
        -- On MinGW, GCC precompiled headers don't preserve #undef state for calling
        -- convention macros. Use textual force-include instead of .gch precompilation.
        add_cxflags("-include TiltedOnlinePCH.h", {force = true})
    else
        set_pcxxheader("TiltedOnlinePCH.h")
    end

    -- exclude game specific stuff and Vivox
    add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
    add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**|link_stubs.cpp|skse_entry.cpp")

    -- Feature-gated packages and targets (per D-01: per-feature guards)
    if not is_plat("mingw") then
        add_defines("HAS_CEF=1", "HAS_DISCORD=1", "HAS_DIRECTXTK=1")
        add_packages("discord", "cef")
        add_deps("SkyrimCoopUIProcess", "SkyrimCoopUI")

        after_install(function(target)
            -- copy dlls
            for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
                local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
                local bindir = path.join(linkdir, "..", "bin")
                os.cp(bindir, target:installdir())
            end
            -- copy ui
            local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
            os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
            os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
            os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
        end)
    else
        -- MinGW: no CEF, no Discord, no DirectXTK
        -- ImGui stays for Phase 6 readiness (D-04)
        -- Force-include MinGW compatibility header after PCH to fix calling convention macros
        add_cxflags("-include MinGWCompat.h", {force = true})
    end

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    -- Add server dependency for embedded GameServer (P2P hosting)
    add_deps("SkyrimTogetherServer")

    -- Core dependencies (always needed)
    add_deps(
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "SkyrimCoopNetworking",
        "SkyrimCoopReverse",
        "SkyrimCoopHooks",
        {inherit = true}
    )

    -- Core packages
    add_packages(
        "rpmalloc",
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "enet6",
        "minhook",
        "entt",
        "glm",
        "xbyak")

    -- mem is header-only; use package on MSVC, local vendored copy on MinGW
    if not is_plat("mingw") then
        add_packages("mem")
    else
        add_includedirs("../external/mem", {public = false})
    end

    -- ImGui unconditional for Phase 6 readiness (D-04)
    add_packages("imgui")

    if has_config("vivox") then
        add_files("Services/Vivox/**.cpp")
        add_headerfiles("Services/Vivox/**.h")
        add_includedirs("Services/Vivox")
        add_deps("Vivox")
        add_defines("TP_VIVOX=1")
    else
        add_defines("TP_VIVOX=0")
    end

    add_syslinks(
        "version",
        "dbghelp",
        "kernel32")
end


build_client("SkyrimTogetherClient")

-- MinGW-only: Thin DLL wrapper that links the static client lib
-- and exports SKSE plugin entry points (SKSEPlugin_Version, SKSEPlugin_Load, DllMain)
-- Equivalent of immersive_launcher's /WHOLEARCHIVE but producing a DLL instead of EXE
if is_plat("mingw") then
    target("SkyrimTogetherClientDLL")
        set_kind("shared")
        set_group("Client")
        set_basename("SkyrimTogetherClient")  -- output: SkyrimTogetherClient.dll
        set_prefixname("")                    -- no "lib" prefix on MinGW

        add_files("skse_entry.cpp", "link_stubs.cpp")
        add_deps("SkyrimTogetherClient")

        -- Remove stale import libraries before linking. Without this, the linker
        -- picks up a tiny .dll.a import lib instead of the 1.5GB static archive.
        before_link(function (target)
            local builddir = target:targetdir()
            for _, f in ipairs({
                "libSkyrimTogetherClient.dll.a",
                "SkyrimTogetherClient.dll.a"
            }) do
                local p = path.join(builddir, f)
                if os.isfile(p) then
                    os.rm(p)
                end
            end
        end)

        -- Statically link MinGW runtime (no libgcc/libstdc++/libwinpthread DLL deps)
        -- xmake strips add_ldflags for shared targets, so use add_shflags
        add_shflags(
            "-static-libgcc",
            "-static-libstdc++",
            "-Wl,-Bstatic", "-lstdc++", "-lpthread",
            "-Wl,-Bdynamic",
            "-Wl,--allow-multiple-definition",
            "-Wl,-Map,SkyrimTogetherClient.map",
            {force = true})

        -- Devbuild: internal deps are shared DLLs, so linking is fast
        -- No need for full static archive chain -- just link against import libs
        -- NOTE: Do NOT use --export-all-symbols here -- the final DLL has >65535 symbols
        -- which exceeds PE/COFF ordinal limits. Only SKSE entry points need exporting
        -- (handled by the existing .def file / __declspec(dllexport) attributes).

        -- Extra system libraries needed at final link (beyond what deps inherit)
        add_syslinks(
            "comctl32",
            "gdi32",
            "dwmapi",
            "stdc++fs")

        -- Include paths needed for skse_entry.cpp, link_stubs.cpp, and TiltedCore headers
        add_includedirs(".", "../external/", "..", "Games/Skyrim")
        add_includedirs("../external/mem", {public = false})
    target_end()
end
