// The package's shipped surface: every SDK library the exports map names has
// a declaration file and a runtime stub that are current with the fixture in
// src/, and the build driver parses. Run with `npm test` after `npm run build`.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import test from 'node:test'
import { fileURLToPath, pathToFileURL } from 'node:url'

const packageRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..')
const manifest = JSON.parse(fs.readFileSync(path.join(packageRoot, 'package.json'), 'utf8'))
const sdk = await import(pathToFileURL(path.join(packageRoot, 'dist/index.js')).href)

const libraryExports = Object.entries(manifest.exports)
  .filter(([specifier, target]) => /^\.\/[A-Z]/.test(specifier) && typeof target === 'object' && target.types)
  .map(([specifier, target]) => ({ name: specifier.slice(2), ...target }))

test('the exports map names every library of the SDK fixture, and nothing else', () => {
  const fixtureLibraries = sdk.windowsSdkFixture.libraries.map((library) => library.name).sort()
  assert.deepEqual(libraryExports.map((entry) => entry.name).sort(), fixtureLibraries)
})

test('generated declarations and runtime stubs are current with src/', () => {
  const declarations = sdk.generateWindowsDeclarations(sdk.windowsSdkFixture)
  const runtimes = sdk.generateWindowsRuntimeModules(sdk.windowsSdkFixture)
  for (const entry of libraryExports) {
    const specifier = `@geastack/windows/${entry.name}`
    assert.ok(declarations[specifier], `${specifier} has declarations`)
    assert.ok(runtimes[specifier], `${specifier} has a runtime module`)
    assert.equal(fs.readFileSync(path.join(packageRoot, entry.types), 'utf8'), declarations[specifier], `${entry.types} is regenerated (npm run build)`)
    assert.equal(fs.readFileSync(path.join(packageRoot, entry.import), 'utf8'), runtimes[specifier], `${entry.import} is regenerated (npm run build)`)
  }
})

test('runtime stubs load but refuse to run outside a native build', async () => {
  const kernel32 = await import(pathToFileURL(path.join(packageRoot, 'runtime/Kernel32.js')).href)
  assert.equal(typeof kernel32.GetTickCount64, 'function')
  assert.throws(() => kernel32.GetTickCount64(), /native|Windows/i)
  const controls = await import(pathToFileURL(path.join(packageRoot, 'runtime/Controls.js')).href)
  assert.equal(typeof controls.WinLabel, 'function')
  assert.throws(() => new controls.WinLabel(), /native|Windows/i)
})

test('the bridge metadata names a thunk for every function and class member', () => {
  const metadata = sdk.generateWindowsBridgeMetadata(sdk.windowsSdkFixture)
  assert.ok(Object.keys(metadata.classes).length >= 15, 'the Controls classes are in the metadata')
  assert.ok(Object.keys(metadata.classes).every((key) => key.startsWith('Controls.')), 'classes are keyed by library')
  const header = sdk.generateWindowsNativeBridgeHeader(metadata)
  for (const library of sdk.windowsSdkFixture.libraries) {
    for (const fn of library.functions ?? []) {
      if (fn.inline) continue
      assert.ok(header.includes(`${library.name}::${fn.name}`) || header.includes(fn.name), `${library.name}.${fn.name} has a thunk prototype`)
    }
  }
  assert.ok(header.includes('WinStackView_addArrangedSubview'), 'instance methods are thunks')
  assert.ok(header.includes('WinLabel_create'), 'constructors are thunks')
})

test('the build driver and the target sources are in place', () => {
  const driver = path.join(packageRoot, 'targets/win32/build-windows.mjs')
  const check = spawnSync(process.execPath, ['--check', driver], { encoding: 'utf8' })
  assert.equal(check.status, 0, check.stderr)
  for (const relative of [
    'targets/win32/app.manifest.in',
    'targets/win32/main/win32_main.cpp',
    'targets/win32/main/win32_renderer.cpp',
    'targets/win32/main/win32_widgets.cpp',
    'targets/win32/main/win32_native_shell.cpp',
    'targets/win32/main/native/controls.cpp',
    'targets/win32/main/native/libraries.cpp',
    'targets/win32/main/bridge/win32_storage_bridge.cpp',
    'geatsc-plugin.mjs',
  ]) {
    assert.ok(fs.existsSync(path.join(packageRoot, relative)), `${relative} exists`)
  }
})
