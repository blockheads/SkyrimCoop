package("directxtk")
    set_homepage("https://github.com/microsoft/DirectXTK")
    set_description("The DirectX Tool Kit (aka DirectXTK) is a collection of helper classes for writing DirectX 11.x code in C++ (patched for Wine MSVC builds)")
    set_license("MIT")

    add_urls("https://github.com/microsoft/DirectXTK/archive/refs/tags/$(version).tar.gz",
             "https://github.com/microsoft/DirectXTK.git")

    add_versions("dec2023", "4a9e5b6e535f6c2c6b18e6f88c19861eb835ac78a7e3ddd02d8d1368e5be8c65")
    add_versions("21.11.0", "de4e4d72f89c875b555c3c22edccb86a59168f8c70e0da20ecf38165e1adbfd7")

    add_deps("cmake")

    on_install("windows", function (package)
        -- Patch CMakeLists.txt to skip building xwbtool utility
        -- xwbtool.cpp is missing from the package but we don't need it for the library
        local cmakelists = path.join(os.curdir(), "CMakeLists.txt")
        if os.isfile(cmakelists) then
            -- Read entire file
            local content = io.readfile(cmakelists)

            -- Comment out the entire xwbtool section (lines 254-264)
            -- Pattern matches from "add_executable(xwbtool" to the closing "target_link_libraries(xwbtool version.lib)"
            content = content:gsub(
                "add_executable%(xwbtool\n    xwbtool/xwbtool%.cpp\n    Audio/WAVFileReader%.cpp\n    Audio/WAVFileReader%.h%)\n  target_include_directories%(xwbtool PRIVATE Audio Src%)\n  target_link_libraries%(xwbtool version%.lib%)",
                "# Patched for Wine MSVC: xwbtool.cpp is missing, but library builds fine without tools\n  # add_executable(xwbtool\n  #   xwbtool/xwbtool.cpp\n  #   Audio/WAVFileReader.cpp\n  #   Audio/WAVFileReader.h)\n  # target_include_directories(xwbtool PRIVATE Audio Src)\n  # target_link_libraries(xwbtool version.lib)"
            )

            io.writefile(cmakelists, content)
        end

        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_TOOLS=OFF")  -- Disable all tools including xwbtool
        table.insert(configs, "-DBUILD_XAUDIO_WIN10=OFF")
        table.insert(configs, "-DBUILD_XAUDIO_WIN8=OFF")

        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <DirectXTK/SimpleMath.h>
            void test() {
                DirectX::SimpleMath::Vector3 v(1.0f, 2.0f, 3.0f);
            }
        ]]}, {configs = {languages = "c++14"}}))
    end)
package_end()
