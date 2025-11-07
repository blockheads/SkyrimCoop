
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_deps("SkyrimEncoding", "TiltedCore")
    add_packages(
        "hopscotch-map",
        "catch2",
        "rpmalloc",
        "glm")
