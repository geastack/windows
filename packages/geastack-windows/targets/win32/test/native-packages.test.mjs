// Native contributions of packages and apps to a Windows build: which
// packages count, how their sources are compiled, and how a runtime capability
// a package requires is provided, staged and named to it.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {
  NativeBuildError,
  nativeDefineFlags,
  nativeSourceLanguage,
  readNativePackages,
  resolveRuntimeProviders,
  runtimeStagingPlan,
} from '../native-packages.mjs'

function write(file, text = '') {
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, typeof text === 'string' ? text : `${JSON.stringify(text, null, 2)}\n`)
}

// An app with two native packages, one of which its program uses.
function fixture(t, { usedDescriptor, unusedDescriptor = { windows: { sources: ['native/unused.cpp'] } } } = {}) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'gea-native-packages-')))
  t.after(() => fs.rmSync(root, { recursive: true, force: true }))
  const appDir = path.join(root, 'app')
  write(path.join(appDir, 'package.json'), { name: 'app', dependencies: { 'used-native': '1.0.0', 'unused-native': '1.0.0' } })
  write(path.join(appDir, 'index.ts'), 'import "used-native"\n')
  const used = path.join(appDir, 'node_modules', 'used-native')
  write(path.join(used, 'package.json'), { name: 'used-native', version: '1.0.0' })
  write(path.join(used, 'src', 'index.ts'), 'export {}\n')
  write(path.join(used, 'native', 'host.mm'), '')
  write(path.join(used, 'native', 'support.c'), '')
  write(path.join(used, 'include', 'host.h'), '')
  write(path.join(used, 'gea-native.json'), usedDescriptor ?? { windows: { sources: ['native/host.mm', 'native/support.c'], includeDirs: ['include'], systemLibraries: ['d3d12.lib'], defines: { USED_NATIVE: 1 } } })
  const unused = path.join(appDir, 'node_modules', 'unused-native')
  write(path.join(unused, 'package.json'), { name: 'unused-native', version: '1.0.0' })
  write(path.join(unused, 'native', 'unused.cpp'), '')
  write(path.join(unused, 'gea-native.json'), unusedDescriptor)
  const moduleGraphPath = path.join(root, 'out', 'module-graph', 'gea-module-graph.json')
  write(moduleGraphPath, { modules: [{ file: path.join(appDir, 'index.ts') }, { file: path.join(used, 'src', 'index.ts') }] })
  return { root, appDir, used, unused, moduleGraphPath }
}

const glesRequirement = {
  gles: {
    description: 'an EGL and OpenGL ES implementation',
    files: { egl: { define: 'TEST_EGL_LIBRARY', default: 'libEGL.dll' }, gles: { define: 'TEST_GLES_LIBRARY', default: 'libGLESv2.dll' } },
  },
}

test('only packages owning a module of the program contribute, with their declared sources', (t) => {
  const { appDir, used, moduleGraphPath } = fixture(t)
  const packages = readNativePackages({ appDir, moduleGraphPath })
  assert.deepEqual(packages.map((entry) => entry.name), ['used-native'])
  const [entry] = packages
  assert.deepEqual(entry.sources, [path.join(used, 'native', 'host.mm'), path.join(used, 'native', 'support.c')])
  assert.deepEqual(entry.includeDirs, [path.join(used, 'include')])
  assert.deepEqual(entry.systemLibraries, ['d3d12.lib'])
  assert.deepEqual(entry.sources.map(nativeSourceLanguage), ['cxx', 'c'])
  assert.deepEqual(nativeDefineFlags({ nativePackages: packages, runtimeProviders: [] }), ['/DUSED_NATIVE=1'])
})

test('without a module graph the declared dependencies count', (t) => {
  const { appDir, moduleGraphPath } = fixture(t)
  fs.rmSync(moduleGraphPath)
  assert.deepEqual(readNativePackages({ appDir, moduleGraphPath }).map((entry) => entry.name), ['unused-native', 'used-native'])
})

test('a declared source that is missing or of an unknown language fails the build', (t) => {
  const missing = fixture(t, { usedDescriptor: { windows: { sources: ['native/absent.cpp'] } } })
  assert.throws(() => readNativePackages(missing), (error) => error instanceof NativeBuildError && /used-native names a source .*absent\.cpp/.test(error.message))
  const foreign = fixture(t, { usedDescriptor: { windows: { sources: ['src/index.ts'] } } })
  assert.throws(() => readNativePackages(foreign), (error) => error instanceof NativeBuildError && /cannot compile: src[\\/]index\.ts/.test(error.message))
})

test('a required capability with no provider fails with what to configure', (t) => {
  const { appDir, moduleGraphPath } = fixture(t, { usedDescriptor: { windows: { sources: ['native/host.mm'], requires: glesRequirement } } })
  const nativePackages = readNativePackages({ appDir, moduleGraphPath })
  assert.throws(
    () => resolveRuntimeProviders({ appDir, nativePackages, providers: {} }),
    (error) =>
      error instanceof NativeBuildError &&
      /used-native needs a 'gles' provider on Windows: an EGL and OpenGL ES implementation/.test(error.message) &&
      /gea\.targets\.windows\.providers\.gles/.test(error.message) &&
      /egl \(default libEGL\.dll\), gles \(default libGLESv2\.dll\)/.test(error.message),
  )
  // A program that does not use the requiring package needs no provider.
  const unusedRequires = fixture(t, { unusedDescriptor: { windows: { requires: glesRequirement } } })
  const packages = readNativePackages(unusedRequires)
  assert.deepEqual(resolveRuntimeProviders({ appDir: unusedRequires.appDir, nativePackages: packages, providers: {} }), [])
})

test('a provider directory supplies the default files, and a missing one is named', (t) => {
  const { appDir, moduleGraphPath } = fixture(t, { usedDescriptor: { windows: { requires: glesRequirement } } })
  const nativePackages = readNativePackages({ appDir, moduleGraphPath })
  write(path.join(appDir, 'angle', 'libEGL.dll'), 'egl')
  assert.throws(
    () => resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: 'angle' } }),
    (error) => error instanceof NativeBuildError && /has no gles file libGLESv2\.dll/.test(error.message),
  )
  write(path.join(appDir, 'angle', 'libGLESv2.dll'), 'gles')
  const providers = resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: 'angle' } })
  assert.deepEqual(nativeDefineFlags({ nativePackages, runtimeProviders: providers }), ['/DTEST_EGL_LIBRARY="libEGL.dll"', '/DTEST_GLES_LIBRARY="libGLESv2.dll"'])
  assert.deepEqual(runtimeStagingPlan({ nativePackages, runtimeProviders: providers }).map((file) => file.to), ['libEGL.dll', 'libGLESv2.dll'])
  assert.throws(
    () => resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: 'missing-dir' } }),
    (error) => error instanceof NativeBuildError && /provider directory .* does not exist/.test(error.message),
  )
})

test('a configured provider names its files and stages its companions where they are', (t) => {
  const { appDir, moduleGraphPath } = fixture(t, { usedDescriptor: { windows: { requires: glesRequirement, runtimeFiles: ['native/support.c'] } } })
  const nativePackages = readNativePackages({ appDir, moduleGraphPath })
  const directory = path.join(appDir, 'build', 'renderer')
  write(path.join(directory, 'OneGL.dll'), 'both')
  write(path.join(directory, 'runtime', 'Companion.dll'), 'companion')
  const runtimeProviders = resolveRuntimeProviders({
    appDir,
    nativePackages,
    providers: { gles: { directory: 'build/renderer', files: { egl: 'OneGL.dll', gles: 'OneGL.dll' }, runtimeFiles: ['runtime/Companion.dll'], defines: { RENDERER_FLAVOUR: 2 } } },
  })
  assert.deepEqual(nativeDefineFlags({ nativePackages, runtimeProviders }), ['/DRENDERER_FLAVOUR=2', '/DTEST_EGL_LIBRARY="OneGL.dll"', '/DTEST_GLES_LIBRARY="OneGL.dll"'])
  assert.deepEqual(runtimeStagingPlan({ nativePackages, runtimeProviders }), [
    { from: path.join(appDir, 'node_modules', 'used-native', 'native', 'support.c'), to: 'support.c' },
    { from: path.join(directory, 'OneGL.dll'), to: 'OneGL.dll' },
    { from: path.join(directory, 'runtime', 'Companion.dll'), to: 'runtime/Companion.dll' },
  ])
  assert.throws(
    () => resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: { directory: 'build/renderer', files: { egl: '../../package.json', gles: 'OneGL.dll' } } } }),
    (error) => error instanceof NativeBuildError && /leaves the provider directory/.test(error.message),
  )
})

test('a provider can be a package that provides the capability', (t) => {
  const { appDir, moduleGraphPath } = fixture(t, { usedDescriptor: { windows: { requires: glesRequirement } } })
  const nativePackages = readNativePackages({ appDir, moduleGraphPath })
  const provider = path.join(appDir, 'node_modules', 'gl-provider')
  write(path.join(provider, 'package.json'), { name: 'gl-provider', version: '1.0.0' })
  write(path.join(provider, 'bin', 'gl.dll'), 'gl')
  write(path.join(provider, 'gea-native.json'), { windows: { provides: { gles: { directory: 'bin', files: { egl: 'gl.dll', gles: 'gl.dll' } } } } })
  const [resolved] = resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: { package: 'gl-provider' } } })
  assert.equal(resolved.directory, path.join(provider, 'bin'))
  assert.deepEqual(resolved.files.map((file) => [file.role, file.relative]), [['egl', 'gl.dll'], ['gles', 'gl.dll']])
  write(path.join(provider, 'gea-native.json'), { windows: {} })
  assert.throws(
    () => resolveRuntimeProviders({ appDir, nativePackages, providers: { gles: { package: 'gl-provider' } } }),
    (error) => error instanceof NativeBuildError && /does not provide 'gles'/.test(error.message),
  )
})

test('two different files staged under one name fail the build', () => {
  const runtimeProviders = [
    { files: [{ full: path.resolve('a', 'x.dll'), relative: 'x.dll' }], runtimeFiles: [] },
    { files: [{ full: path.resolve('b', 'X.dll'), relative: 'X.dll' }], runtimeFiles: [] },
  ]
  assert.throws(() => runtimeStagingPlan({ nativePackages: [], runtimeProviders }), (error) => error instanceof NativeBuildError && /two different files would be staged as X\.dll/.test(error.message))
})
