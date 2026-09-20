# @geastack/geatsc-plugin-windows-native

The geatsc compiler plugin behind `@geastack/windows`. It is an external
`CompilerPlugin`; the compiler itself knows nothing about Windows.

Given the SDK's bridge metadata it:

- declares the `@geastack/windows` and `@geastack/windows/*` modules to the
  compiler and maps every library function, class member, constructor and
  constant onto a C++ thunk in the `gea::windows` namespace (host tables);
- rewrites JSX over the Controls classes (`<WinStackView spacing={8}>…`)
  into construction and `addArrangedSubview` / `addSubview` calls;
- writes `gea/windows/native_bridge.h` and `native_bridge.cpp` into the
  build's generated directory, the contract the target's C++ implements.

The bridge is written only for a build that names its metadata (the
`windows.metadata` plugin option or the `GEA_WINDOWS_NATIVE_METADATA`
environment variable, both set by `build-windows.mjs`). Installed for an app
built for the web, the plugin declares the modules and writes nothing.

`@geastack/windows` re-exports this plugin from its `geatsc-plugin` entry,
which is how the CLI finds it for any app that depends on `@geastack/windows`.

## License

Apache-2.0. See `LICENSE`.
