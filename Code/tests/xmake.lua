-- Skip tests on MinGW cross-compilation (tests run natively on Linux)
-- Tests can still be built on native Windows or Linux
target("TPTests")
    set_kind("binary")
    set_group("Tests")

    add_includedirs(".", "../encoding", "../relay_dll")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_files("../relay_dll/command_queue.cpp")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "glm")
