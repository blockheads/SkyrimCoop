-- Message encoding: serialization, quantization, differentials (no game).
target("TPTests")
    set_kind("binary")
    set_group("Tests")

    add_includedirs(".", "../encoding", "../common", "../base", "../external")
    add_files("encoding.cpp", "main.cpp")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "glm", "spdlog", "rpmalloc")

includes("host_authority")
includes("client")
includes("games")
includes("peer")
