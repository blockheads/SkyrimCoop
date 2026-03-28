-- This function defines the core component idoms
function component(name)
  target(name)
    devbuild_shared()
    set_group("Components")
    add_configfiles("BuildInfo.h.in")
    add_includedirs(
      ".",
      "../",
      "../../",
      "../../../build",
      {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp|**Test.cpp|**Test*.cpp")
    add_deps("TiltedCore")
    add_packages(
      "hopscotch-map",
      "spdlog")
end

-- this isnt fully specified yet.
function unittest(name)
    target(name .. "_Tests")
      set_kind("binary")
      set_group("Tests")
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
includes("es_loader")
includes("crash_handler")
includes("resources")

-- Tier 3 client components (require Windows/MinGW)
if is_plat("windows") or is_plat("mingw") then
    includes("imgui")
end
