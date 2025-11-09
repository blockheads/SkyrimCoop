-- ImGui static library build (manually vendored to avoid Wine MSVC package issues)
target("imgui")
    set_kind("static")
    set_group("External")

    -- Core imgui files
    add_files(
        "imgui.cpp",
        "imgui_draw.cpp",
        "imgui_tables.cpp",
        "imgui_widgets.cpp",
        -- Include misc/cpp for std::string support
        "misc/cpp/imgui_stdlib.cpp"
    )

    -- Public include directory
    add_includedirs(".", {public = true})
    add_includedirs("misc/cpp", {public = true})

    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    end
