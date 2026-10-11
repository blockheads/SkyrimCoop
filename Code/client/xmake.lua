
local function build_client(name)
target(name)
    if is_plat("mingw", "windows") then
        -- Objects, not a static archive: every .obj is linked into the DLL. From an
        -- archive the linker dropped the ~38 files only referenced through
        -- self-registering TiltedPhoques::Initializer hooks (ActorMagicCaster,
        -- SkyrimVM64, UI, ...), so their hooks never installed. This matches the
        -- MSVC build's /WHOLEARCHIVE and also skips re-archiving ~350MB per build.
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Client")
    add_includedirs(".","../external/")
    -- Add server include directory for embedded GameServer (P2P hosting)
    add_includedirs("../server", {public = false})
    if is_plat("mingw") and not is_mode("dev") then
        -- On MinGW, GCC precompiled headers don't preserve #undef state for calling
        -- convention macros. Use textual force-include instead of .gch precompilation.
        add_cxflags("-include TiltedOnlinePCH.h", {force = true})
    else
        -- dev mode on MinGW: MinGWCompat.h is force-included after the PCH below,
        -- which restores the calling convention macros (verified: identical symbols).
        set_pcxxheader("TiltedOnlinePCH.h")
    end

    -- exclude game specific stuff and Vivox
    if is_plat("mingw") then
        -- MinGW: also exclude Debug views (DebugService is gated off for __MINGW32__)
        add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**|Services/Debug/**")
        add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**|Services/Debug/**|link_stubs.cpp|skse_entry.cpp")
    else
        add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
        add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**|link_stubs.cpp|skse_entry.cpp")
    end

    -- Feature-gated packages and targets (per D-01: per-feature guards)
    if not is_plat("mingw", "windows") then
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
        -- SKSE builds: no CEF, no Discord, no DirectXTK library
        -- But HAS_DIRECTXTK is needed to gate SKSE-typed struct fields (ActivateEvent, etc.)
        add_defines("HAS_DIRECTXTK=1")
        -- ImGui stays for Phase 6 readiness (D-04)
        if is_plat("mingw") then
            -- Force-include MinGW compatibility header after PCH to fix calling convention macros
            add_cxflags("-include MinGWCompat.h", {force = true})
        end
    end

    add_files("Bridges/SkyrimBridge.cpp")
    -- Resident loader: game hooks never point into the replaceable module.
    add_files("../hotreload/MovementLoader.cpp")
    add_headerfiles("Bridges/*.h")
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
        "enet6",
        "minhook",
        "entt",
        "glm",
        "xbyak",
        "nlohmann_json")

    -- mem is header-only; vendored copy
    add_includedirs("../external/mem", {public = false})

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

-- SKSE plugin DLL: links every client object and exports the SKSE entry points
-- (SKSEPlugin_Version, SKSEPlugin_Load, DllMain). MinGW, or clang-cl ("windows").
if is_plat("mingw", "windows") then
    target("SkyrimTogetherClientDLL")
        set_kind("shared")
        set_group("Client")
        set_basename("SkyrimTogetherClient")  -- output: SkyrimTogetherClient.dll
        set_prefixname("")                    -- no "lib" prefix on MinGW

        add_files("skse_entry.cpp")
        if is_plat("mingw") then
            add_files("link_stubs.cpp")
        end
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
        if is_plat("mingw") then add_shflags(
            "-static-libgcc",
            "-static-libstdc++",
            "-Wl,-Bstatic", "-lstdc++", "-lpthread",
            "-Wl,-Bdynamic",
            "-Wl,--allow-multiple-definition",
            "-Wl,-Map,SkyrimTogetherClient.map",
            {force = true}) end

        -- Devbuild: internal deps are shared DLLs, so linking is fast
        -- No need for full static archive chain -- just link against import libs
        -- NOTE: Do NOT use --export-all-symbols here -- the final DLL has >65535 symbols
        -- which exceeds PE/COFF ordinal limits. Only SKSE entry points need exporting
        -- (handled by the existing .def file / __declspec(dllexport) attributes).

        -- Extra system libraries needed at final link (beyond what deps inherit)
        add_syslinks(
            "comctl32",
            "gdi32",
            "dwmapi")
        if is_plat("mingw") then
            add_syslinks("stdc++fs")
        end

        -- Include paths needed for skse_entry.cpp, link_stubs.cpp, and TiltedCore headers
        add_includedirs(".", "../external/", "..", "Games/Skyrim")
        add_includedirs("../external/mem", {public = false})
    target_end()
end

-- ============================================================================
-- SkyrimTogetherClientServices — compilation-boundary static lib
--
-- Compiles Services/**/*.cpp + Bridges/ReplayBridge.cpp WITHOUT any
-- Games/Skyrim include path on the include list.  This is the proof that
-- migrated services carry zero SKSE dependencies.
--
-- NOTE: This target will NOT compile today — services still include SKSE
-- headers directly.  Plans 02.5-02 and 02.5-03 migrate each service to use
-- the bridge interfaces instead.  Define the target now so the build system
-- enforces the boundary as soon as a service is migrated.
-- ============================================================================
target("SkyrimTogetherClientServices")
    set_kind("static")
    -- Nothing links this; building it by default compiled the services twice.
    set_default(false)
    set_group("Client")
    set_languages("cxx20")

    -- PCH setup: service files rely on TiltedOnlinePCH.h being pre-included.
    -- On MinGW, use force-include (same as main client); on other platforms use standard PCH.
    if is_plat("mingw") then
        add_cxflags("-include TiltedOnlinePCH.h", {force = true})
        add_cxflags("-include MinGWCompat.h", {force = true})
    else
        set_pcxxheader("TiltedOnlinePCH.h")
    end

    -- Services and bridge (no Games/Skyrim here)
    -- Excluded (still SKSE-dependent, not in plan 03 migration scope):
    --   Debug/          - DebugService and views require Actor.h, ImGui, HWND
    --   CalendarService - inherits BSTEventSink<TESActivateEvent>, uses TimeData::Get()
    --   PapyrusService  - uses GameVM::Get()->virtualMachine
    add_files("Services/Generic/ActorValueService.cpp",
              "Services/Generic/CharacterService.cpp",
              "Services/Generic/CombatService.cpp",
              "Services/Generic/CommandService.cpp",
              "Services/Generic/DiscordService.cpp",
              "Services/Generic/ImguiService.cpp",
              "Services/Generic/InputService.cpp",
              "Services/Generic/InventoryService.cpp",
              "Services/Generic/MagicService.cpp",
              "Services/Generic/MapService.cpp",
              "Services/Generic/ObjectService.cpp",
              "Services/Generic/OverlayClient.cpp",
              "Services/Generic/OverlayService.cpp",
              "Services/Generic/PlayerService.cpp",
              "Services/Generic/QuestService.cpp",
              "Services/Generic/RunnerService.cpp",
              "Services/Generic/StringCacheService.cpp",
              "Services/Generic/TransportService.cpp",
              "Services/Generic/WeatherService.cpp")
    add_files("Services/HostService.cpp")
    add_files("Bridges/ReplayBridge.cpp")
    add_headerfiles("Bridges/*.h")

    -- Include paths
    -- NOTE: Games/Skyrim IS on the path (for RTTI.h, Utils.h which TiltedOnlinePCH.h needs),
    -- but SKSE type headers (Actor.h, TESObjectCELL.h, etc.) must NOT appear in any
    -- migrated service source file — that is enforced by CI grep, not by include path exclusion.
    add_includedirs(".", "../external/", "Games/Skyrim")
    add_includedirs("Bridges")
    -- Server include dir needed for HostService (embedded GameServer)
    add_includedirs("../server", {public = false})

    -- Mirror the main client package set (TiltedOnlinePCH.h pulls in xbyak, imgui, etc.)
    add_packages(
        "rpmalloc",
        "spdlog",
        "hopscotch-map",
        "enet6",
        "minhook",
        "entt",
        "glm",
        "xbyak",
        "imgui",
        "nlohmann_json")

    -- mem is header-only; vendored copy
    add_includedirs("../external/mem", {public = false})

    -- Encoding + networking + core deps needed by TiltedOnlinePCH.h
    -- SkyrimTogetherServer needed for HostService (embedded server)
    add_deps("SkyrimEncoding", "SkyrimCoopNetworking", "SkyrimCoopReverse", "BaseLib", "CommonLib",
             "SkyrimTogetherServer")
target_end()
