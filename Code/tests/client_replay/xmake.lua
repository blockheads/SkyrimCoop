-- ClientReplayTests — headless bridge smoke tests
--
-- Links against SkyrimTogetherClientServices (the SKSE-free services lib)
-- and ReplayBridge, proving the compilation boundary holds.
-- Does NOT reference native_client or any SKSE-bearing target.

target("ClientReplayTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx20")

    -- Test sources
    add_files("*.cpp")

    -- Include paths — no Games/Skyrim allowed
    add_includedirs("../../client")
    add_includedirs("../../client/Bridges")
    add_includedirs("../../encoding")
    add_includedirs("../../external")

    -- Dependencies
    add_deps("SkyrimEncoding")
    -- NOTE: SkyrimTogetherClientServices is listed here as the future dep.
    -- Until services are migrated (Plans 02.5-02/03), keep it commented to
    -- avoid build failures blocking the test binary itself.
    -- add_deps("SkyrimTogetherClientServices")

    -- Packages
    add_packages(
        "catch2",
        "entt",
        "spdlog",
        "nlohmann_json",
        "glm")

    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()
