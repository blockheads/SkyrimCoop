-- The test_harness option (root xmake.lua). COOP_TEST_HARNESS is always defined, 0 or 1, for
-- `#if COOP_TEST_HARNESS`.
local function add_test_harness_define()
    add_defines(has_config("test_harness") and "COOP_TEST_HARNESS=1" or "COOP_TEST_HARNESS=0")
end

local function build_client(name)
target(name)
    -- Objects, not a static archive: every .obj is linked into the DLL. From an archive the linker
    -- dropped the ~38 files only referenced through self-registering Hooks::Initializer hooks
    -- (ActorMagicCaster, SkyrimVM64, UI, ...), so their hooks never installed.
    set_kind("object")
    set_group("Client")
    add_includedirs(".","../external/")
    set_pcxxheader("ClientPch.h")
    -- offsetof checks on engine structs (non-standard layout by design) and clang-cl's PCH include rule: expected.
    add_cxflags("-Wno-invalid-offsetof", "-Wno-microsoft-include", {tools = {"clang_cl"}})

    add_headerfiles("**.h|Games/Skyrim/**|TestHarness/**|HotReload/**")
    add_files("**.cpp|Games/Skyrim/**|TestHarness/**|HotReload/**|skse_entry.cpp")
    -- HotReload/Handoff.h is used by production code either way: without the harness it is no-ops.
    add_headerfiles("HotReload/Handoff.h")
    add_test_harness_define()
    if has_config("test_harness") then
        add_files("TestHarness/**.cpp", "HotReload/**.cpp")
        add_headerfiles("TestHarness/**.h", "HotReload/**.h")
    end

    add_files("Bridges/SkyrimBridge.cpp")
    add_headerfiles("Bridges/*.h")
    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")

    -- Core dependencies (always needed)
    add_deps(
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "Network",
        {inherit = true}
    )

    -- Core packages
    add_packages(
        "rpmalloc",
        "spdlog",
        "enet6",
        "minhook",
        "entt",
        "glm",
        "xbyak",
        "nlohmann_json")

    -- mem is header-only; vendored copy
    add_includedirs("../external/mem", {public = false})

    -- ImGui (debug views), RmlUi (the in-game UI, Code/client/Ui)
    add_packages("imgui", "rmlui")
    add_syslinks("d3dcompiler", "windowscodecs", "ole32", "version", "dbghelp", "kernel32", "dxguid", "dinput8", "d3d11")
end


build_client("SkyrimTogetherClient")

-- SKSE plugin DLL: links every client object and exports the SKSE entry points
-- (SKSEPlugin_Version, SKSEPlugin_Load, DllMain).
target("SkyrimTogetherClientDLL")
    set_kind("shared")
    set_group("Client")
    set_basename("SkyrimTogetherClient")
    add_files("skse_entry.cpp")
    add_test_harness_define()
    add_deps("SkyrimTogetherClient")
    add_syslinks("gdi32", "dwmapi")
    add_includedirs(".", "../external/", "..", "Games/Skyrim")
    add_includedirs("../external/mem", {public = false})
target_end()
