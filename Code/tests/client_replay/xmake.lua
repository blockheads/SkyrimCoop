-- ClientReplayTests — headless bridge replay tests
--
-- Proves the SKSE abstraction layer (BUILD-04): services boundary compiles
-- without SKSE headers, and ReplayBridge reads NDJSON fixtures for headless testing.
-- Does NOT reference native_client, World, or any SKSE-bearing target.

target("ClientReplayTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx20")

    -- Test sources (harness + test cases)
    add_files("*.cpp")

    -- ReplayBridge implementation (SKSE-free, part of client/Bridges)
    add_files("../../client/Bridges/ReplayBridge.cpp")

    -- Include paths — no Games/Skyrim, no SKSE allowed
    add_includedirs(".")
    add_includedirs("../../client")
    add_includedirs("../../client/Bridges")
    add_includedirs("../../encoding")
    add_includedirs("../../external")

    -- TiltedCore include path for TiltedPhoques:: types (Buffer, Map, String etc.)
    add_includedirs("../../TiltedCore")

    -- Dependencies
    add_deps("SkyrimEncoding", "TiltedCore")
    -- NOTE: SkyrimTogetherClientServices is NOT linked here per Pitfall 6:
    -- the services lib may pull in SKSE-bearing compilation units.
    -- The test proves the bridge compiles and links without SKSE.

    -- Packages
    add_packages(
        "catch2",
        "entt",
        "spdlog",
        "nlohmann_json",
        "glm",
        "hopscotch-map",
        "rpmalloc")

    if is_plat("linux") then
        add_syslinks("pthread")
    end

    -- Run tests from project root so relative fixture paths resolve correctly
    set_rundir("$(projectdir)")
target_end()
