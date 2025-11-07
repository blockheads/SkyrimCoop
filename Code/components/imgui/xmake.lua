
-- ImGui is Windows-only (also works for MinGW cross-compile)
if is_plat("windows") or is_plat("mingw") then
    component("ImGuiImpl")
        add_packages("imgui")
end
