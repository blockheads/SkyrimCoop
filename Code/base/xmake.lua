
target("BaseLib")
    add_configfiles("BuildInfo.h.in")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("common")
    add_includedirs(".", "../", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")
    add_deps("TiltedCore")
    add_packages(
        "sentry-native",
        "hopscotch-map",
        "gtest",
        "spdlog")
