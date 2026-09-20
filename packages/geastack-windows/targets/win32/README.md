# Win32 target

Build a Gea app as a native Windows desktop application — `<Button>` becomes a
`BUTTON`, `<Text>` a GDI-drawn label window, `<View>` a child window painting
its own background and corner radii, `<Image>` an `AlphaBlend`ed bitmap window,
`<canvas>` a window blitting the element's pixel surface, `<input>` an `EDIT`
(or a trackbar / check box by type), `<textarea>` a multi-line `EDIT`,
`<virtual-list>` and `overflow: scroll` views a scroll container with a native
scroll bar. The flex layout engine in `@geastack/engine` computes frames; the
target positions windows and paints.

## Build

    node targets/win32/build-windows.mjs <app-id> [--run]

or, from an app that depends on `@geastack/windows`, `gea build --target windows`.

## Where the build writes

Everything lands under the **app project**, never under this package:
`<app>/dist/windows/<app-id>/<AppName>.exe` for the executable,
`<app>/dist/windows/<app-id>/build/` for objects and dependency files,
`<app>/dist/windows/<app-id>/Resources/` for the window config, fonts and
sounds, and `<app>/dist/windows/.generated/<app-id>/` for the generated C++.
`GEA_WINDOWS_OUTPUT_DIR` moves the tree.

Without app metadata the build falls back to the built-in smoke app
(`main/win32_smoke_app.cpp`), which exercises View / Text / Button / event
dispatch.

## Architecture

    JSX (or smoke C++) → Document / Tree mutations → Tree::computeLayout
      → Renderer::sync (walks the laid-out tree)
        → materializationFor dispatches on NodeType + tag
          → GeaView / GeaLabel / BUTTON / EDIT / trackbar / GeaImage
            / GeaCanvas / GeaScroll
          → applyWidgetStyle (frame, background, border, radii, opacity,
            text, value, range)

Two layers:

- **`win32_widgets.{h,cpp}`** — the widget layer. One `Widget` record per
  child HWND with a `WidgetStyle` and `WidgetEvents`. Custom classes
  (`GeaView`, `GeaLabel`, `GeaImage`, `GeaCanvas`, `GeaScroll`, `GeaDivider`)
  are painted here, double-buffered, with GDI+ for rounded shapes and GDI for
  text. Common controls are wrapped through a subclass so their notifications
  arrive as the same events. All geometry is device pixels.
- **`win32_renderer.{h,cpp}`** — the reconciler from the gea tree to widgets.
  Nodes map to widget kinds, style fields to `WidgetStyle`, layout boxes to
  frames (scaled by DPI); a node whose materialization changed (an input that
  became a range) is rebuilt; nodes that vanished are swept.

The Controls bridge (`main/native/controls.cpp`) sits on the same widget layer:
a Windows-native program's `WinStackView` is a `GeaView` widget whose children
this file arranges.

The display-list pipeline (`ui/render.cpp`, `ui/dirty_regions.cpp`) is bypassed
on this target — Windows owns invalidation. The framework's touch/momentum code
in `ui/input.cpp` is bypassed too; container presses fire touchstart / touchend
/ click into `Tree::dispatchEvent`, and `GeaScroll` gives lists native scrolling.

## Frame loop

`win32_main.cpp` paces frames at the monitor's refresh rate from the message
pump, waiting for input between frames. Each tick:

1. `Application::frame(now)` lets the app update state
2. The CSS animation clock advances
3. The mounted root's `style.width/height` are forced to the content area in
   layout px — the target convention that makes the viewport track resize
4. `Tree::computeLayout(root, w, h)`
5. `Renderer::sync(contentHost, root)`
6. `localStorage` writes made this frame are persisted

`<glass-split>` roots take the split-shell path (`win32_native_shell.cpp`):
each pane is laid out against its own resizable container and the toolbar
becomes a title-bar strip. Windows-native apps take the Controls layout path.

## What works / what's stubbed

Native: View / Text / Button / Image / Canvas / VirtualList; input, textarea,
range, checkbox, progress; symbol glyphs; flex layout; click + press + input +
keydown events; native scrolling and wheel routing; DPI scaling; dark mode;
window resize; the split shell with draggable dividers and a toolbar.

Real platform hooks: WinHTTP fetch, XAudio2 audio, file-backed localStorage,
private font registration, battery status, the app launcher.

Stubbed: IMU (flat), embedded memory diagnostics (zeros), BLE (no driver).
CSS rotate / blur / box-shadow are not representable with child windows and are
ignored. Resident-app bundles are a follow-up.
