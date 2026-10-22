-- This function defines the core component idoms
function component(name)
  target(name)
    set_kind("static")
    set_group("Components")
    add_includedirs(
      ".",
      "../",
      "../../",
      "../../../build",
      {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp|**Test.cpp|**Test*.cpp")
    add_deps("TiltedCore")
    add_packages("spdlog")
end

-- Client components (Windows only)
if is_plat("windows") then
    includes("imgui")
end
