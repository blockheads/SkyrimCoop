
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "TiltedCore",
        "hopscotch-map",
        "catch2",
        "rpmalloc",
        "glm")
