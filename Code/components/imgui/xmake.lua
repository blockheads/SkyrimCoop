if is_plat("windows") or is_plat("mingw") then
    component("ImGuiImpl")
        add_deps("imgui")
        -- System libraries needed when building as shared DLL (devbuild mode)
        -- D3DCompile from d3dcompiler_47, GDI functions from gdi32, DWM from dwmapi
        add_syslinks("d3d11", "d3dcompiler", "gdi32", "dwmapi")
end
