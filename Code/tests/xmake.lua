-- Message encoding: serialization, quantization, differentials (no server, no game).
-- The server and native_client tests beside it are not built; they go with the server.
target("TPTests")
    set_kind("binary")
    set_group("Tests")

    add_includedirs(".", "../encoding", "../common", "../base", "../external")
    add_files("encoding.cpp", "main.cpp")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "rpmalloc")

includes("bridge")
includes("integration")
includes("replay")
includes("client_replay")
