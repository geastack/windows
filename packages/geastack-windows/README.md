# @geastack/windows

The Windows target for GeaStack apps, in one package:

- **Windows SDK bindings** an app imports as ES modules with full types:
  `@geastack/windows/User32`, `Kernel32`, `Shell32`, `Dwmapi`, `Win32`
  (structs and helpers) and `Controls` (native window, stack, label, text
  field, text view, button, check box, slider, progress bar, image, scroll,
  box, split view and toolbar classes). The compiler lowers every call to a
  thunk in the native bridge; the runtime modules here only throw, so a web
  build that reaches one fails loudly.
- **The Win32 desktop target** (`targets/win32`): `build-windows.mjs` turns
  an app into a native `.exe` with clang-cl and lld-link, and the C++ under
  `targets/win32/main` is the renderer (a painted surface per window, real
  child windows only for native controls and scroll containers), the
  glass-split shell, the platform hooks (display, timers, memory, sensors,
  WinHTTP network, storage, XAudio2 audio) and the Controls object model.

## Use

```json
{
  "dependencies": { "@geastack/windows": "^0.1.0" },
  "gea": { "targets": { "windows": true } }
}
```

```bash
gea build --target windows
gea run --target windows
```

A Windows-native app (`"runtime": "windows-native"`) builds its UI from the
Controls classes instead of gea components and adds
`@geastack/vite-plugin-windows-native` to its Vite config.

```ts
import { installRootView, WinLabel, WinStackView } from '@geastack/windows/Controls'

const root = new WinStackView()
const label = new WinLabel()
label.text = 'Hello from Win32'
root.addArrangedSubview(label)
installRootView(root)
```

## Requirements

Windows 10 or later, LLVM (clang-cl, lld-link, llvm-rc) either standalone or
as the "C++ Clang tools for Windows" component of Visual Studio Build Tools,
the MSVC headers and a Windows SDK. `build-windows.mjs` finds them; set
`GEA_WINDOWS_CLANG_CL` / `GEA_WINDOWS_LLD_LINK` to point at a specific
toolchain.

## Layout

| Path | What |
| --- | --- |
| `src/index.ts` | The SDK fixture and the generators for declarations, runtime stubs, bridge metadata and the native bridge header/source |
| `generated/*.d.ts`, `runtime/*.js` | Generated from the fixture (`npm run build`) |
| `targets/win32/build-windows.mjs` | The build driver (`gea build --target windows` runs it) |
| `targets/win32/main/` | The target's C++ |
| `targets/win32/test/` | `npm test` |

## License

Apache-2.0. See `LICENSE`.
