# Windows Development Guide

This document explains how to work on the Windows repo without losing track of
which layer owns which behavior.

## Repo Responsibilities

The Windows repo owns three related surfaces:

1. The native Win32 target shell: window, frame loop, the component-tree
   renderer over child windows, and the `<glass-split>` shell.
2. Windows SDK binding metadata and the TypeScript packages generated from it.
3. The Controls object model (`WinView`, `WinStackView`, ...) that
   Windows-native programs build against, implemented over the same widget
   layer the renderer uses.

It does not own generic TypeScript lowering, Gea runtime components, embedded
board support, or example app source. Those live in neighboring repos.

## Package Resolution

The build resolves every package through `node_modules`, walking up from the
app being built and from this package. Nothing assumes a particular checkout
layout: the `@geastack/*` packages come from npm, installed alongside the app.
To test an unreleased change, link that one checkout into the app's
`node_modules/@geastack/` (junctions work) and leave the rest installed.

## Package Workflow

```sh
cd packages/geastack-windows
npm run typecheck
npm run build        # tsc, then scripts/generate.mjs rewrites generated/ + runtime/
```

- `@geastack/windows`: the SDK fixture (`src/index.ts`), its generators, the
  generated declarations and runtime stubs, and the Win32 target.
- `@geastack/geatsc-plugin-windows-native`: the compiler plugin. It imports the
  SDK package to build host tables, so build the SDK first.
- `@geastack/vite-plugin-windows-native`: plain ESM, no build step.

Adding to the SDK: add the class, method, property, function or constant to
`windowsSdkFixture`, run `npm run build`, then implement the new thunk in
`targets/win32/main/native/controls.cpp` (Controls) or `libraries.cpp` (raw
APIs). An unimplemented thunk is a link error naming it, which is the intended
failure mode.

## Win32 Target Workflow

```sh
node targets/win32/build-windows.mjs <app-id> [--run] [--clean] [--debug] [--jobs N] [--verbose]
```

Run it from the app's folder (the CLI does this). Without app
metadata the build produces the smoke app.

| Variable | Purpose |
| --- | --- |
| `GEA_WINDOWS_CLANG_CL`, `GEA_WINDOWS_LLD_LINK` | Pin the compiler / linker. |
| `GEA_WINDOWS_JOBS` | Compile parallelism (default: CPUs, at most 12). |
| `GEA_WINDOWS_OPT` | Optimization flag (default `/O2`; `--debug` uses `/Od` with symbols). |
| `GEA_WINDOWS_OUTPUT_DIR` | Move the whole output tree. |
| `GEA_WINDOWS_TIMINGS=1` | Print per-phase timings. |
| `GEA_CPP_TRANSLATION_UNITS` | Generated unit layout (default `balanced`). |

Runtime verification hooks: `GEA_WINDOWS_VERIFY_ONCE=1`, `GEA_WINDOWS_SCREENSHOT=<png>`
(+ `GEA_WINDOWS_SCREENSHOT_FRAME`, `GEA_WINDOWS_SCREENSHOT_STAY`),
`GEA_WINDOWS_SYNTH_INPUT`, `GEA_WINDOWS_SYNTH_CLICK`, `GEA_WINDOWS_LAYOUT_DUMP`,
`GEA_WINDOWS_TICK_DEBUG`, `GEA_WINDOWS_EXIT_AFTER_MS`, `GEA_WINDOWS_WIN_W/H`.

### Why clang-cl, and why /force:multiple

The framework declares its platform hooks `__attribute__((weak))` and generated
programs define some of them weakly. MSVC has no weak symbols at all, so the
target compiles with clang-cl. On COFF a weak symbol becomes a "weak external"
with a per-object default alias, and two objects naming different defaults for
one symbol is a hard error in both `link.exe` and `lld-link`. The driver links
with `/force:multiple`, which keeps the last default seen, and orders the
objects framework → target → generated program so every weak reference lands on
the real definition. The build prints how many weak symbols were merged;
`--verbose` lists them. A TU that both includes a weak declaration and defines
the symbol becomes a weak definition itself, which is why `win32_display.cpp`
declares the orientation hooks without the header.

## Verification Checklist

Before landing target changes, run the smallest relevant checks:

- TypeScript package work: `npm run typecheck` and `npm run build` in the
  touched package; the notes-windows example's `tsc --noEmit` against the
  regenerated declarations.
- Renderer work: build the smoke app (`build-windows.mjs smoke` outside any
  app) and run it with `GEA_WINDOWS_VERIFY_ONCE=1`; build `notes-jsx` and
  compare a `GEA_WINDOWS_SCREENSHOT` capture.
- Controls / bridge work: build `notes-windows` and exercise folder selection,
  note selection, typing in the title and body, New Note, and the sidebar
  toggle.
- CLI dispatch changes: `npm test` in the CLI repo (`gea.test.mjs` covers the
  Windows target).

Document any check that cannot run locally.
