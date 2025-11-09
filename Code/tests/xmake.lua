
-- Skip tests on Wine MSVC builds due to missing core.tools.lib module
-- Tests can still be built on native Windows or Linux
target("TPTests")
    set_kind("binary")
    set_group("Tests")

    -- Disable on Wine MSVC (when using /opt/msvc SDK)
    if get_config("sdk") == "/opt/msvc" then
        set_enabled(false)
    end

    add_includedirs(
        ".", "../encoding")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "TiltedCore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm")
