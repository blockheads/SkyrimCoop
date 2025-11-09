-- This function defines the core component idoms
function component(name)
  target(name)
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Components")
    add_configfiles("BuildInfo.h.in")
    add_includedirs(
      ".",
      "../",
      "../../",
      "../../../build",
      {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")
    add_deps("TiltedCore")
    add_packages(
      "hopscotch-map",
      "gtest",
      "spdlog")
end

-- this isnt fully specified yet.
function unittest(name)
    target(name .. "_Tests")
      set_kind("binary")
      set_group("Tests")

      -- Disable on Wine MSVC (missing core.tools.lib for linking)
      if get_config("sdk") == "/opt/msvc" then
        set_enabled(false)
      end
      add_configfiles("BuildInfo.h.in")
      add_includedirs(
        ".",
        "../",
        "../../",
        "../../../build",
        {public = true})
      add_headerfiles(
          "**.h")
      add_files(
          "**.cpp",
          "../../TestMain.cpp")
      add_deps("TiltedCore")
      add_packages(
        "hopscotch-map",
        "gtest",
        "spdlog")
  end

-- List all components required below:
includes("console")
includes("imgui")
includes("es_loader")
includes("crash_handler")
includes("resources")	