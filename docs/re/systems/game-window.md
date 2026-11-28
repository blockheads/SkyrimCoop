# The game window: closing it, resizing its swap chain

The window's procedure, the loop that pumps it, and what runs when the window is closed or resized. Read in IDA on
1.7.104 (2026-11-27) for the playtest crash `crash_UTC_2026-11-26_07-36-25.dmp` (a guest on Windows, AMD, native DXGI).

## The crash

`SkyrimSE.exe+0x1012512`, a read through null: `Renderer::CreateSwapChainRenderTarget` (0x141012480) used a back
buffer that `IDXGISwapChain::GetBuffer` did not give. The dump's registers and stack:

- Main thread: `WinMain` (36544) → `Main::Update` → `Main::sub_36555` (the frame's render step) → `Renderer::Begin`
  → `Renderer::WindowSizeChanged` (77238) → `CreateSwapChainRenderTarget`.
- In `WindowSizeChanged`'s frame, the RECT `GetWindowRect` should have filled was stack garbage (right = a stale
  `user32` return address, bottom 0x7FFC): `GetWindowRect` had failed, so the window handle (`0x1C86110E`, still in
  the renderer's window 0) was gone. The stored size was 1920x1080, not fullscreen.
- The player's report: the game was closed (no freeze, no crash log written by the player). The guest had just lost
  its host and was reconnecting.

## The path (IDA)

1. `WindowProc` (36649, 0x14065F270) has no `WM_CLOSE` case: the X button and Alt+F4 (`WM_SYSCOMMAND SC_CLOSE` →
   `WM_CLOSE`) go to `DefWindowProcA`, which destroys the window. Its `WM_DESTROY` case sets `Main::quitGame`
   (`Main` + 0x10, CommonLibSSE `RE::Main`).
2. `WinMain`'s loop pumps messages, then runs the frame (`ProcessEvents_Enter` → `Main::Update`) if the window is
   active **or `bAlwaysActive` (380768) is set**, and only then tests `quitGame`. Our plugin sets `bAlwaysActive`
   every update (`Plugin::Update`), so the frame after `WM_DESTROY` always runs, on a window that is gone.
3. `Renderer::Begin` calls `WindowSizeChanged` every frame. It reads the window's size with `GetWindowRect` and
   `GetFullscreenState`, and checks no result: the garbage RECT differs from the stored size, so it releases the
   swap chain's views (`ClearState`, `Flush`), calls `ResizeBuffers(0, 0, 0, 0, 2)` (result unchecked), then
   `CreateSwapChainRenderTarget`.
4. `CreateSwapChainRenderTarget` calls `GetBuffer(0)`, `CreateRenderTargetView`, `CreateShaderResourceView` and the
   texture's `GetDesc`, with no HRESULT checked. With the window destroyed the resize has no buffer to give: the
   views come back `E_INVALIDARG` (RAX at the crash) and `GetDesc` on the null texture faults.

Our D3D11 code holds no back-buffer reference (`UiRenderer` and ImGui's DX11 backend keep the bound state only
between their begin and end, on the same thread as `Renderer::Begin`; there is no Present or ResizeBuffers hook), so
the suspicion that we block the resize was wrong: a live window resizes fine.

## The fix

`Hook_WndProc` (`engine/BSGraphics/BSGraphicsRenderer.cpp`) answers `WM_CLOSE` by setting `Main::quitGame` and
returning 0, as the console's `qqq` quits: the window stays, the frame in progress ends normally, the loop breaks and
the game's own teardown (`Renderer::Shutdown`) destroys the window. Keeping `bAlwaysActive` (the game must simulate
unfocused) is why vanilla's order is unsafe for us.

Test: `coop_test.py repro quit-close-window` launches a solo game, sends `WM_CLOSE` (harness `close_window`) and
requires the process to exit with no crash log and no frame on the destroyed window. Under Wine the frame does not
crash (DXVK still gives a buffer after the failed resize), so the harness logs one ("a frame ran after the game's window
was destroyed", `IsWindow` in `TestHarness::Poll`) and the recipe fails on that line: it did before the fix, and
passes 2 of 2 after. No GameTests flow: the window procedure and the frame order are Win32 and engine, with no logic
in `sync` for the fake engine to run.
