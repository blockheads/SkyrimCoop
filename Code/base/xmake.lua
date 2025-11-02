
target("BaseLib")
    add_configfiles("BuildInfo.h.in")
    set_kind("static")
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
