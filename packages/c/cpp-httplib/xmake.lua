package("cpp-httplib")
    set_homepage("https://github.com/yhirose/cpp-httplib")
    set_description("A C++11 single-file header-only cross platform HTTP/HTTPS library (patched for Wine MSVC builds)")
    set_license("MIT")

    add_urls("https://github.com/yhirose/cpp-httplib/archive/refs/tags/$(version).tar.gz",
             "https://github.com/yhirose/cpp-httplib.git")

    add_versions("0.14.0", "c6abb90b0924b6e0e8e82b38e73c1bf052a554ad86ab1e97f1bae3c47e66cb6e")

    -- Override on_check to skip Windows version validation for MSVC-wine builds
    on_check(function (package)
        -- Skip all checks for cross-compilation scenarios
        -- We're building with MSVC via Wine on Linux, so winos.version() doesn't apply
    end)

    on_install(function (package)
        -- Patch the header to remove Windows version check for MSVC-wine compatibility
        local httplib_h = path.join(os.curdir(), "httplib.h")
        if os.isfile(httplib_h) then
            io.gsub(httplib_h, "#if defined%(_WIN32_WINNT%) && _WIN32_WINNT < 0x0602",
                    "#if 0  // Patched: removed Windows version check for MSVC-wine")
            io.gsub(httplib_h, '#error "cpp%-httplib doesn\'t support Windows 8 or lower%. Please use Windows 10 or later%."', '')
        end

        -- Copy header to install directory
        os.cp("httplib.h", package:installdir("include"))
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <httplib.h>
            void test() {
                httplib::Client cli("http://example.com");
            }
        ]]}, {configs = {languages = "c++11"}}))
    end)
package_end()
