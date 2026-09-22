# GeaStack Windows

Native Windows target support for GeaStack.

This repo contains the packages that let Gea applications run as native Win32
desktop apps instead of browser views: the Win32 target (a renderer that maps
the Gea component tree onto native child windows and common controls), generated
Windows SDK bindings, and the compiler plugin that lowers calls into those
bindings.

## What Is Here

| Path | Purpose |
| --- | --- |
| `packages/geastack-windows` | `@geastack/windows`: generated Windows SDK declarations and runtime stubs (User32, Kernel32, Shell32, Dwmapi, Win32 structs, and the `Controls` object model), plus the Win32 target under `targets/win32`. |
| `packages/geastack-windows/targets/win32` | The target: `build-windows.mjs` (Node build driver), `main/` (window, frame loop, renderer, split shell, platform hooks), `main/native/` (the Controls and raw-API thunks), `include/` (POSIX shims the framework's desktop arms need). |
| `packages/geatsc-plugin-windows-native` | `@geastack/geatsc-plugin-windows-native`: geatsc compiler plugin stating the Windows host tables and writing the native bridge beside the emitted program. |
| `packages/vite-plugin-windows-native` | `@geastack/vite-plugin-windows-native`: Vite plugins for Windows-native apps (JSX over controls, module-graph recording, empty GEA IR). |
| `docs` | Repo-level development notes for package and target maintainers. |

## Quick Start

In an app that declares `"windows": true` under `gea.targets` and depends on
`@geastack/windows`:

```sh
npx gea build --target windows
npx gea run --target windows
```

Or drive the target directly from the package:

```sh
node packages/geastack-windows/targets/win32/build-windows.mjs <app-id> --run
```

Build the TypeScript packages from their folders:

```sh
cd packages/geastack-windows && npm run build      # tsc + regenerate generated/ and runtime/
cd packages/geatsc-plugin-windows-native && npm run build
```

## Dependencies

- clang-cl and lld-link (LLVM 17+, standalone or the Visual Studio "C++ Clang
  tools for Windows" component). MSVC's `cl.exe` cannot compile the framework's
  weak-symbol hooks.
- Visual Studio Build Tools with the MSVC C++ toolset and a Windows 10/11 SDK,
  for the standard library and platform headers.
- Node.js 22+.
- The rest of the stack from npm: `@geastack/core`, `@geastack/compiler`,
  `@geastack/geatsc-plugin-gea` and the CLI, installed in the app being built.

## Documentation

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): development workflow, target
  responsibilities and verification checklist.
- [packages/geastack-windows/targets/win32/README.md](packages/geastack-windows/targets/win32/README.md):
  renderer and target architecture.

## How This Fits The Stack

The Windows target consumes compiled Gea apps and maps Gea primitives onto
native controls where possible, as the Apple targets do for AppKit and
UIKit. This repo is the Windows platform adapter layer. It owns the native
shell, the generated SDK binding surface, and the Windows build script.

## Maintenance Notes

- `generated/` and `runtime/` are build outputs of `src/index.ts`; edit the SDK
  fixture there and run `npm run build`, never the generated files.
- Keep the SDK binding package source-compatible with the compiler plugin that
  consumes its metadata, and keep every thunk the generated header declares
  implemented under `targets/win32/main/native/`.
- The smoke app (`win32_smoke_app.cpp`) exists to prove the renderer and event
  plumbing before a generated Gea app is available. Keep it small.
- When adding target behavior, update the target README and
  [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) in the same change.

## License

Apache-2.0 (see `LICENSE`). You can ship closed-source products
built on it. The only GeaStack code under a different license is
the embedded board support (`targets` and `@geastack/chips`, GPL-3.0-only):
shipping closed-source firmware through those needs a commercial license.
Contact [contact@geastack.com](mailto:contact@geastack.com) for commercial terms, support and hosted builds.
