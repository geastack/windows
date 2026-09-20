# @geastack/vite-plugin-windows-native

Vite plugins for a Windows-native GeaStack app (`"runtime": "windows-native"`
in the app's `gea` manifest). The native compiler compiles the app's
TypeScript sources; Vite runs only so the module graph is walked and
recorded for it.

```ts
// vite.config.ts
import { windowsNativeVitePlugins } from '@geastack/vite-plugin-windows-native'

export default {
  plugins: windowsNativeVitePlugins(),
}
```

`windowsNativeVitePlugins()` returns three plugins:

- `windowsNativeJsxPlugin()` rewrites `<WinStackView …>` and the other
  Controls classes into ordinary construction before esbuild sees the file,
  since these apps configure no JSX factory;
- `geaEmptyIrPlugin()` writes the empty gea IR the build pipeline expects
  from an app with no gea components;
- `geaModuleGraphPluginsFromCore()` loads the module-graph recorder from
  `@geastack/core`, which the build points at its output directory.

Peer dependency: `@geastack/core`.

## License

Apache-2.0. See `LICENSE`.
