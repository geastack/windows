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

The C++ is generated again only when an input changed since the last
generation (`.gea-generation.stamp`) or the generation settings differ
(`.gea-build-env`). The inputs are the app directory's sources, every module
the last generation compiled (from its module graph, so sources the entry
imports from outside the app directory, e.g. `../src`, count; a listed module
that no longer exists forces a new generation), the project's tsconfig and the
configs it `extends`, and the framework, plugins and compiler. Otherwise the
build only compiles and links what changed.

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

Implemented platform hooks: WinHTTP fetch, XAudio2 audio, file-backed localStorage,
private font registration, battery status, the app launcher.

Stubbed: IMU (flat), embedded memory diagnostics (zeros), BLE (no driver).
CSS rotate / blur / box-shadow are not representable with child windows and are
ignored. Resident-app bundles are a follow-up.

## Native code from packages and the app

A package carrying native code for this target declares it in its own
`gea-native.json`; the build compiles and links it for every package that owns
a module of the compiled program (the app's declared dependencies when no
module graph was written). See `native-packages.mjs` for the full format.

```json
{
  "windows": {
    "sources": ["native/host.mm", "native/surface.cpp"],
    "includeDirs": ["native"],
    "libraries": ["vendor/lib/thing.dll.lib"],
    "systemLibraries": ["d3d12.lib"],
    "runtimeFiles": ["vendor/lib/thing.dll"],
    "defines": { "THING_STATIC": 1 },
    "requires": {
      "gles": {
        "description": "an EGL and OpenGL ES 3 implementation",
        "files": { "egl": { "define": "HOST_EGL_LIBRARY", "default": "libEGL.dll" } }
      }
    }
  }
}
```

`.c` and `.m` sources compile as C and `.cpp`/`.cc`/`.cxx`/`.mm` as C++ (an
`.mm` keeps its Objective-C behind `__APPLE__`). A capability in `requires`
is supplied by the app, in `gea.targets.windows.providers.<capability>`: a
directory holding the files (each role defaults to its `default`), an object
`{ "directory", "files": { "<role>": "<file>" }, "runtimeFiles": [...] }`, or
`{ "package": "<name>" }` naming a package whose `gea-native.json` has the same
object under `windows.provides.<capability>`. The build stages the role files
and runtime files beside the executable, keeping their paths relative to the
provider directory, and passes each role's file to the package as a string
define. A program that uses a package whose requirement nobody provides fails
the build with a message naming what to configure.

The app's own native code for this target sits in its manifest:

```json
"gea": {
  "targets": {
    "windows": {
      "nativeSources": ["windows/services.cpp", "windows/window.cpp"],
      "nativeIncludeDirectories": ["windows/include"],
      "nativeSystemLibraries": ["windowscodecs"],
      "nativeResources": { "assets": "build/assets" },
      "tsconfig": "tsconfig.native.json",
      "prebuild": "node scripts/prepare-assets.mjs",
      "providers": { "gles": { "directory": "build/renderer", "files": { "egl": "GL.dll", "gles": "GL.dll" } } }
    }
  }
}
```

`gea.nativeSources` (shared by every target) contributes its C and C++ files
here too. `nativeResources` copies each directory into `Resources/<name>`.
`tsconfig` names the TypeScript project the compiler reads the program under.
`prebuild` runs in the app folder before anything else (`gea build` runs it).
`gea.defines` apply to the whole build.

## Host extensions

Native code that owns part of the main window -- a GPU swap chain the program
draws into, an app's own window behaviour -- registers a host extension from a
static initializer (`include/gea_win32_host.h`). The target attaches every
extension after creating the main window and before the program's top level
runs, brackets the program's initialization and each frame with
`begin_frame`/`end_frame`, offers it every main-window message first and
reports content-area and DPI changes through `layout`.
`gea_win32_set_frame_pacing(GEA_WIN32_FRAME_PACING_PRESENT)` hands frame pacing
to the extension's presentation (frames run back to back; a vsynced present
blocks), instead of the default refresh-rate timer. A program that mounts no
component tree shows only the extensions' windows.

## Native build settings

Typed settings under `gea.targets.windows.build.native` (or `gea.build.native`):

| Setting | Values | Effect |
| --- | --- | --- |
| `jobs` | integer, 0 | parallel compiles (0: min(cores, 12); `--jobs` and `GEA_WINDOWS_JOBS` win) |
| `arch` | `default`, `AVX2`, `AVX512` | `/arch` for every unit |
| `debugInfo` | boolean | `/Z7` and `/debug` in optimized code; identical-code folding stays on |
| `lto` | `off`, `thin` | ThinLTO through lld-link |
| `ltoJobs`, `ltoCache` | integer, path | ThinLTO backend threads (0: all), cache directory |
| `ltoExcludeSources` | pattern | sources built as ordinary objects under ThinLTO |
| `unoptimizedSources` | pattern | sources built at `/Od` (very large constant-data units) |
| `pgo`, `pgoProfile` | `off`, `generate`, `use`; path | instrumented build, or one optimized from a merged `.profdata` |
| `package` | `none`, `zip` | `dist/windows/<Name>-Windows.zip`: the executable, its runtime files and `Resources` in one `<Name>/` folder |

Patterns are regular expressions over the source path with forward slashes.
`GEA_WINDOWS_PGO=generate` (or `off`, `use`) overrides `pgo` for one build, e.g.
the instrumented training build of an app that ships with `pgo: "use"`; an
instrumented executable writes its profile (`LLVM_PROFILE_FILE`) when it exits.
`GEA_GEATSC_MAX_OLD_SPACE_SIZE` (MiB) changes the compiler's heap limit.
