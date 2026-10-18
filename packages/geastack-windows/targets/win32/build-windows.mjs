#!/usr/bin/env node
// Builds a Gea app as a native Win32 desktop executable.
//
//   node targets/win32/build-windows.mjs <app-id> [--run] [--clean] [--debug] [--jobs N] [--verbose]
//
// The pipeline, in order:
//   1. resolve the framework packages through node_modules and the toolchain
//      (clang-cl + lld-link; the MSVC headers and Windows SDK they find);
//   2. ask the CLI for the app's manifest (root, entry, runtime, name);
//   3. generate C++ from the app's TypeScript with the shared
//      build-gea-vite-geatsc.mjs pipeline (skipped when nothing changed);
//   4. compile every framework, target and generated unit in parallel with
//      per-object dependency tracking, so a rebuild recompiles only what moved;
//   5. link the executable with the app manifest and icon embedded;
//   6. stage Resources/ (window.json, fonts, sounds).
//
// Everything lands under the app project: <app>/dist/windows/<app-id>/<Name>.exe,
// objects in <app>/dist/windows/<app-id>/build/, generated C++ in
// <app>/dist/windows/.generated/<app-id>/. GEA_WINDOWS_OUTPUT_DIR moves the tree.
//
// The project is the directory this runs in -- `gea build` starts it in the
// app's own folder. Environment:
// GEA_EXTRA_GEATSC_PLUGINS (path-delimited compiler plugins), GEA_CLI_BIN,
// GEA_GEATSC_BIN, GEA_WINDOWS_CLANG_CL, GEA_WINDOWS_LLD_LINK, GEA_WINDOWS_JOBS,
// GEA_WINDOWS_OPT (/O2). Translation-unit layout comes from package.json.

import { spawn, spawnSync } from 'node:child_process'
import crypto from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath, pathToFileURL } from 'node:url'

import {
  NativeBuildError,
  nativeDefineFlags,
  nativeSourceLanguage,
  nativeSourcePattern,
  readNativePackages,
  resolveRuntimeProviders,
  runtimeStagingPlan,
} from './native-packages.mjs'

const targetDir = path.dirname(fileURLToPath(import.meta.url))
const packageRoot = path.resolve(targetDir, '../..')
const args = process.argv.slice(2)

function fail(message) {
  process.stderr.write(`build-windows: ${message}\n`)
  process.exit(1)
}

function flag(name) {
  return args.includes(name)
}

function option(name, fallback = '') {
  const index = args.indexOf(name)
  if (index >= 0 && index + 1 < args.length) return args[index + 1]
  const inline = args.find((argument) => argument.startsWith(`${name}=`))
  return inline ? inline.slice(name.length + 1) : fallback
}

const verbose = flag('--verbose')
const appId = args.find((argument) => !argument.startsWith('--')) ?? 'hello'
const projectDir = process.cwd()

// --- package resolution ----------------------------------------------------------

function resolvePackageFrom(start, name) {
  let dir = start
  for (;;) {
    const candidate = path.join(dir, 'node_modules', name)
    if (fs.existsSync(path.join(candidate, 'package.json'))) return fs.realpathSync(candidate)
    const parent = path.dirname(dir)
    if (parent === dir) return ''
    dir = parent
  }
}

function resolvePackage(name, { optional = false, siblings = [] } = {}) {
  for (const start of [projectDir, packageRoot]) {
    const found = resolvePackageFrom(start, name)
    if (found) return found
  }
  for (const sibling of siblings) {
    if (fs.existsSync(path.join(sibling, 'package.json'))) return sibling
  }
  if (optional) return ''
  return fail(`cannot resolve ${name} from ${projectDir} or ${packageRoot} -- run npm install in the app`)
}

const geaCore = resolvePackage('@geastack/core')
const geaHost = process.env.GEA_HOST_DIR || resolvePackage('@geastack/host', { siblings: [path.join(geaCore, '../host')] })
const geaEngine = process.env.GEA_ENGINE_DIR || resolvePackage('@geastack/engine', { siblings: [path.join(geaCore, '../engine')] })
const geaElements = process.env.GEA_ELEMENTS_DIR || resolvePackage('@geastack/elements', { siblings: [path.join(geaCore, '../elements')] })
const geaGeaos = process.env.GEA_GEAOS_PACKAGE_DIR || resolvePackage('@geastack/geaos', { siblings: [path.join(geaCore, '../geaos')] })
const geaCompiler = resolvePackage('@geastack/compiler')
const geaPlugin = resolvePackage('@geastack/geatsc-plugin-gea', { siblings: [path.join(geaCore, '../geatsc-plugin-gea')] })
const windowsPlugin = resolvePackage('@geastack/geatsc-plugin-windows-native', {
  optional: true,
  siblings: [path.resolve(packageRoot, '../geatsc-plugin-windows-native')],
})
const geaCli = (() => {
  if (process.env.GEA_CLI_BIN) return process.env.GEA_CLI_BIN
  const cliPackage = resolvePackage('@geastack/cli', { optional: true })
  if (cliPackage) return path.join(cliPackage, 'bin', 'gea.mjs')
  const onPath = spawnSync(process.platform === 'win32' ? 'where' : 'which', ['gea'], { encoding: 'utf8' })
  const found = onPath.status === 0 ? onPath.stdout.split(/\r?\n/).find(Boolean) : ''
  return found ? found.trim() : fail('cannot locate the GeaStack CLI -- set GEA_CLI_BIN or install @geastack/cli')
})()

const frameworkEnv = {
  ...process.env,
  GEA_CORE: geaCore,
  GEA_HOST_DIR: geaHost,
  GEA_ENGINE_DIR: geaEngine,
  GEA_ELEMENTS_DIR: geaElements,
  GEA_GEAOS_PACKAGE_DIR: geaGeaos,
}

function manifestQuery(query) {
  const result = spawnSync(process.execPath, [path.join(geaCore, 'gea_sources.mjs'), query], { encoding: 'utf8', env: frameworkEnv })
  if (result.status !== 0) fail(`gea_sources.mjs ${query} failed: ${result.stderr}`)
  return result.stdout.split(/\r?\n/).filter(Boolean)
}

// --- toolchain -------------------------------------------------------------------

function firstExisting(candidates) {
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) ?? ''
}

function onPath(name) {
  const result = spawnSync('where', [name], { encoding: 'utf8' })
  if (result.status !== 0) return ''
  return result.stdout.split(/\r?\n/).find(Boolean)?.trim() ?? ''
}

function visualStudioRoots() {
  const vswhere = firstExisting([
    path.join(process.env['ProgramFiles(x86)'] ?? 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe'),
    path.join(process.env.ProgramFiles ?? 'C:/Program Files', 'Microsoft Visual Studio/Installer/vswhere.exe'),
  ])
  if (!vswhere) return []
  const result = spawnSync(vswhere, ['-all', '-products', '*', '-property', 'installationPath'], { encoding: 'utf8' })
  if (result.status !== 0) return []
  return result.stdout.split(/\r?\n/).map((line) => line.trim()).filter(Boolean)
}

function findClangCl() {
  const stated = process.env.GEA_WINDOWS_CLANG_CL
  if (stated) return fs.existsSync(stated) ? stated : fail(`GEA_WINDOWS_CLANG_CL names a missing file: ${stated}`)
  const fromPath = onPath('clang-cl.exe')
  if (fromPath) return fromPath
  for (const root of visualStudioRoots()) {
    const candidate = path.join(root, 'VC/Tools/Llvm/x64/bin/clang-cl.exe')
    if (fs.existsSync(candidate)) return candidate
  }
  return (
    firstExisting([
      path.join(process.env.ProgramFiles ?? 'C:/Program Files', 'LLVM/bin/clang-cl.exe'),
      path.join(process.env.LOCALAPPDATA ?? '', 'Programs/LLVM/bin/clang-cl.exe'),
    ]) ||
    fail(
      'clang-cl.exe not found. Install LLVM (https://releases.llvm.org) or the "C++ Clang tools for Windows" component of Visual Studio Build Tools, ' +
        'or set GEA_WINDOWS_CLANG_CL. The Windows target compiles with clang-cl because the framework uses clang/GCC extensions MSVC lacks.',
    )
  )
}

const clangCl = findClangCl()
const llvmBin = path.dirname(clangCl)
const lldLink = process.env.GEA_WINDOWS_LLD_LINK || firstExisting([path.join(llvmBin, 'lld-link.exe')]) || onPath('lld-link.exe') || fail(`lld-link.exe not found next to ${clangCl}`)
const llvmRc = firstExisting([path.join(llvmBin, 'llvm-rc.exe')]) || onPath('llvm-rc.exe') || onPath('rc.exe')

// --- app manifest ----------------------------------------------------------------

function inspectApp(format) {
  // No --project: this runs in the project, which is what the CLI resolves
  // from cwd anyway.
  const result = spawnSync(process.execPath, [geaCli, 'apps', 'inspect', appId, '--format', format], {
    encoding: 'utf8',
    env: process.env,
  })
  if (result.status !== 0) return null
  return result.stdout.trim()
}

const appShell = inspectApp('shell')
let appRoot = ''
let appEntry = 'index.tsx'
let appRuntime = 'gea'
let appName = appId.replace(/(^|[-_])(\w)/g, (_, __, letter) => letter.toUpperCase())
let appJson = null
if (appShell) {
  const [root, entry, runtime, name] = appShell.split('\t')
  // `apps inspect` reports an absolute app directory; nothing to join it to.
  appRoot = root
  appEntry = entry || appEntry
  appRuntime = runtime || appRuntime
  appName = name || appName
  try {
    appJson = JSON.parse(inspectApp('json') ?? 'null')
  } catch {
    appJson = null
  }
}
const appDir = appRoot
const exeBaseName = appName.replace(/[<>:"/\\|?*]+/g, '').trim() || appId

// --- output layout ---------------------------------------------------------------

const outputRoot = path.resolve(process.env.GEA_WINDOWS_OUTPUT_DIR ?? path.join(projectDir, 'dist', 'windows'))
const distDir = path.join(outputRoot, appId)
const buildDir = path.join(distDir, 'build')
const generatedDir = path.join(outputRoot, '.generated', appId)
const resourcesDir = path.join(distDir, 'Resources')
const executable = path.join(distDir, `${exeBaseName}.exe`)

if (flag('--clean')) {
  fs.rmSync(distDir, { recursive: true, force: true })
  fs.rmSync(generatedDir, { recursive: true, force: true })
}
fs.mkdirSync(buildDir, { recursive: true })
fs.mkdirSync(resourcesDir, { recursive: true })

const timings = []
let lastMark = Date.now()
function mark(label) {
  const now = Date.now()
  timings.push([label, now - lastMark])
  lastMark = now
}

// --- generation ------------------------------------------------------------------

function newestMtime(dir, predicate, skip = new Set(['node_modules', 'dist', 'build', '.vite', '.git'])) {
  let newest = 0
  const stack = [dir]
  while (stack.length > 0) {
    const current = stack.pop()
    if (!fs.existsSync(current)) continue
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name)
      if (entry.isDirectory()) {
        if (!skip.has(entry.name)) stack.push(full)
        continue
      }
      if (!entry.isFile() || !predicate(entry.name)) continue
      const mtime = fs.statSync(full).mtimeMs
      if (mtime > newest) newest = mtime
    }
  }
  return newest
}

function usesWindowsNative(dir) {
  const stack = [dir]
  while (stack.length > 0) {
    const current = stack.pop()
    if (!fs.existsSync(current)) continue
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name)
      if (entry.isDirectory()) {
        if (!['node_modules', 'dist', 'build', '.vite'].includes(entry.name)) stack.push(full)
        continue
      }
      if (!entry.isFile() || !/\.(?:tsx?|jsx?)$/.test(entry.name)) continue
      const code = fs.readFileSync(full, 'utf8')
      if (code.includes("'@geastack/windows/") || code.includes('"@geastack/windows/')) return true
    }
  }
  return false
}

const windowsNative = appDir !== '' && (appRuntime === 'windows-native' || usesWindowsNative(appDir))
const extraPlugins = (process.env.GEA_EXTRA_GEATSC_PLUGINS ?? '')
  .split(path.delimiter)
  .filter(Boolean)
  .map((plugin) => path.resolve(plugin))
if (windowsNative && windowsPlugin && !extraPlugins.some((plugin) => fs.realpathSync(path.dirname(plugin)).startsWith(fs.realpathSync(windowsPlugin)))) {
  extraPlugins.push(path.join(windowsPlugin, 'dist', 'index.js'))
}

const buildConfigPath = process.env.GEA_BUILD_CONFIG_JSON
const buildConfig = buildConfigPath ? JSON.parse(fs.readFileSync(buildConfigPath, 'utf8')) : { settings: { compiler: { translationUnits: 'balanced' } } }

let generatedSources = []
let generationHappened = false
if (appDir && fs.existsSync(path.join(appDir, appEntry))) {
  const sourceList = path.join(generatedDir, 'geatsc-sources.txt')
  const stamp = path.join(generatedDir, '.gea-generation.stamp')
  const signaturePath = path.join(generatedDir, '.gea-build-env')
  const inputsNewest = Math.max(
    newestMtime(appDir, (name) => /\.(?:tsx?|jsx?|mjs|css|json)$/.test(name)),
    newestMtime(path.join(geaCore, 'scripts'), (name) => name.endsWith('.mjs')),
    newestMtime(path.join(geaPlugin, 'dist'), (name) => name.endsWith('.js')),
    windowsPlugin ? newestMtime(path.join(windowsPlugin, 'dist'), (name) => name.endsWith('.js')) : 0,
    newestMtime(path.join(geaCompiler, 'dist'), (name) => name.endsWith('.js')),
    newestMtime(path.join(packageRoot, 'dist'), (name) => name.endsWith('.js')),
  )
  const translationUnits = buildConfig.settings.compiler?.translationUnits ?? 'balanced'
  // gea.targets.windows.tsconfig: the TypeScript project the compiler reads the program under.
  const tsconfig = appJson?.targetConfig?.windows?.tsconfig ? path.resolve(appDir, appJson.targetConfig.windows.tsconfig) : ''
  const signature = JSON.stringify({ windowsNative, extraPlugins, buildConfig, translationUnits, tsconfig, entry: appEntry, geatsc: process.env.GEA_GEATSC_BIN ?? '' })
  const stampTime = fs.existsSync(stamp) ? fs.statSync(stamp).mtimeMs : 0
  const fresh =
    fs.existsSync(sourceList) && stampTime >= inputsNewest && fs.existsSync(signaturePath) && fs.readFileSync(signaturePath, 'utf8') === signature
  if (!fresh) {
    process.stdout.write(`Generating C++ for app '${appId}' from ${appDir} (entry ${appEntry})...\n`)
    fs.mkdirSync(generatedDir, { recursive: true })
    const generateArgs = [
      path.join(geaCore, 'scripts', 'build-gea-vite-geatsc.mjs'),
      '--app-dir',
      appDir,
      '--entry',
      appEntry,
      '--out-dir',
      generatedDir,
      '--geatsc-bin',
      process.env.GEA_GEATSC_BIN ?? path.join(geaCompiler, 'dist', 'cli.js'),
      '--geatsc-gea-plugin',
      path.join(geaPlugin, 'dist', 'index.js'),
      '--no-apple-native',
    ]
    for (const plugin of extraPlugins) generateArgs.push('--extra-geatsc-plugin', plugin)
    if (tsconfig) generateArgs.push('--project', tsconfig)
    const generateEnv = { ...frameworkEnv }
    if (buildConfigPath) generateArgs.push('--build-config', buildConfigPath)
    else generateArgs.push('--translation-units', translationUnits)
    if (windowsNative) {
      // The bridge metadata the compiler plugin reads: the SDK fixture as
      // bridge metadata, written beside the generated program.
      const sdkModule = path.join(packageRoot, 'dist', 'index.js')
      if (!fs.existsSync(sdkModule)) fail(`missing @geastack/windows build output: ${sdkModule}. Run npm run build in the package first.`)
      const sdk = await import(pathToFileURL(sdkModule).href)
      const metadataPath = path.join(generatedDir, 'gea-windows-metadata.json')
      fs.writeFileSync(metadataPath, `${JSON.stringify(sdk.generateWindowsBridgeMetadata(sdk.windowsSdkFixture), null, 2)}\n`)
      generateEnv.GEA_WINDOWS_NATIVE_METADATA = metadataPath
      generateArgs.push('--compile-module-graph')
    }
    const result = spawnSync(process.execPath, generateArgs, { stdio: 'inherit', env: generateEnv })
    if (result.status !== 0) {
      fs.rmSync(stamp, { force: true })
      fail(`C++ generation failed for '${appId}'`)
    }
    fs.writeFileSync(signaturePath, signature)
    fs.writeFileSync(stamp, new Date().toISOString())
    generationHappened = true
  }
  generatedSources = fs.existsSync(sourceList)
    ? fs
        .readFileSync(sourceList, 'utf8')
        .split(/\r?\n/)
        .filter(Boolean)
        .map((file) => (path.isAbsolute(file) ? file : path.join(generatedDir, file)))
        .filter((file) => fs.existsSync(file))
    : []
} else if (appShell) {
  fail(`'${appId}' has no ${appEntry} under ${appDir}`)
} else {
  process.stdout.write(`No app metadata for '${appId}'; building the smoke app.\n`)
}
mark('generation')

// Windows-native detection from the transformed output: the compiler plugin
// writes the bridge beside the unit when the build named its metadata.
const bridgeHeader = path.join(generatedDir, 'gea', 'windows', 'native_bridge.h')
const bridgeSource = path.join(generatedDir, 'gea', 'windows', 'native_bridge.cpp')
const nativeBridgeActive = windowsNative && fs.existsSync(bridgeHeader) && fs.existsSync(bridgeSource)
if (windowsNative && !nativeBridgeActive) {
  fail(`'${appId}' is a Windows-native app but the compiler wrote no ${path.relative(generatedDir, bridgeHeader)}; is @geastack/geatsc-plugin-windows-native built?`)
}

// --- native packages -------------------------------------------------------------
//
// Packages the program uses declare their native code in gea-native.json, and
// runtime capabilities they need come from providers the app configures; see
// native-packages.mjs for the format and the selection rules.

const windowsTargetConfig = appJson?.targetConfig?.windows ?? {}
let nativePackages = []
let runtimeProviders = []
try {
  nativePackages = readNativePackages({ appDir, moduleGraphPath: path.join(generatedDir, 'module-graph', 'gea-module-graph.json') })
  runtimeProviders = resolveRuntimeProviders({ appDir, nativePackages, providers: windowsTargetConfig.providers })
} catch (error) {
  if (error instanceof NativeBuildError) fail(error.message)
  throw error
}
if (verbose && nativePackages.length > 0) {
  process.stdout.write(`native packages: ${nativePackages.map((entry) => entry.name).join(', ')}\n`)
}
if (verbose) {
  for (const provider of runtimeProviders) process.stdout.write(`provider ${provider.capability}: ${provider.directory}\n`)
}

// App native code for this target: gea.nativeSources (C and C++ only, as
// before -- that list is shared with every target, where a .mm is Apple code)
// and gea.targets.windows.nativeSources, which may also name .mm files.
const appNativeSources = [
  ...(appJson?.nativeSourcePaths ?? appJson?.nativeSources ?? [])
    .map((source) => (path.isAbsolute(source) ? source : path.join(appDir, source)))
    .filter((source) => /\.(?:c|cc|cpp|cxx)$/.test(source)),
  ...(appJson?.targetNativeSourcePaths?.windows ?? []).filter((source) => nativeSourcePattern.test(source)),
].filter((source, index, all) => fs.existsSync(source) && all.indexOf(source) === index)
const appNativeLink = appJson?.nativeLink?.windows ?? {}

// --- sources ---------------------------------------------------------------------

const includeDirs = [
  path.join(targetDir, 'include'),
  path.join(targetDir, 'main'),
  generatedDir,
  ...manifestQuery('include-flags').map((flagText) => flagText.replace(/^-I/, '')),
]
if (appDir) includeDirs.push(appDir, path.join(appDir, 'native'))
for (const entry of nativePackages) includeDirs.push(...entry.includeDirs)
includeDirs.push(...(appNativeLink.nativeIncludeDirectories ?? []))

// Weak symbols on COFF. The framework declares optional hooks weak
// (`__attribute__((weak))`) and generated programs define several of them
// weakly too (asset lookup, cycle-collection deferral). A COFF weak symbol is
// a "weak external" with a per-object default alias, and a weak reference
// meeting a weak definition names two defaults: a duplicate for both link.exe
// and lld-link. The hooks a program defines are therefore referenced as
// ordinary symbols here (GEA_LINKED_HOOK_DEFAULTS), and the target links each
// one's default through /alternatename (main/win32_hook_defaults.cpp), which
// applies only while nothing defines the symbol. Every other duplicate is a
// link error. The objects still go framework first, target second, generated
// program last.
const excludedFramework = /\/(host\/camera|runtime|services\/[a-z_]+)\.cpp$/
const cSources = [...manifestQuery('c-sources'), path.join(targetDir, 'main', 'win32_apps.c')]
const cxxSources = [
  ...manifestQuery('cxx-sources').filter((source) => !excludedFramework.test(source.replace(/\\/g, '/'))),
  path.join(geaCore, 'gea_app_entry.cpp'),
]
if (fs.existsSync(path.join(geaGeaos, 'resident_apps.cpp'))) cxxSources.push(path.join(geaGeaos, 'resident_apps.cpp'))
cxxSources.push(
  ...fs
    .readdirSync(path.join(targetDir, 'main'))
    .filter((name) => name.endsWith('.cpp') && name !== 'win32_smoke_app.cpp')
    .map((name) => path.join(targetDir, 'main', name)),
)
if (nativeBridgeActive) {
  const nativeDir = path.join(targetDir, 'main', 'native')
  if (fs.existsSync(nativeDir)) {
    cxxSources.push(...fs.readdirSync(nativeDir).filter((name) => name.endsWith('.cpp')).map((name) => path.join(nativeDir, name)))
  }
}
const declaredNativeSources = [...nativePackages.flatMap((entry) => entry.sources), ...appNativeSources]
for (const source of declaredNativeSources) {
  if (nativeSourceLanguage(source) === 'c') cSources.push(source)
  else cxxSources.push(source)
}
// Generated program last (see the link-order note above).
if (nativeBridgeActive) cxxSources.push(bridgeSource)
if (generatedSources.length > 0) {
  cxxSources.push(...generatedSources)
} else {
  cxxSources.push(path.join(targetDir, 'main', 'win32_smoke_app.cpp'))
}
const assetsCpp = path.join(generatedDir, 'gea_embedded_assets_generated.cpp')
if (fs.existsSync(assetsCpp)) cxxSources.push(assetsCpp)
// The generated program's own storage bridge: localStorage persistence for the
// compiler runtime's key/value table, compiled against the same runtime
// prelude the generated units use (named in geatsc-header.txt).
const storageBridge = path.join(targetDir, 'main', 'bridge', 'win32_storage_bridge.cpp')
const perUnitFlags = new Map()
if (generatedSources.length > 0 && fs.existsSync(storageBridge)) {
  const headerManifest = path.join(generatedDir, 'geatsc-header.txt')
  let runtimePrelude = ''
  if (fs.existsSync(headerManifest)) {
    for (const line of fs.readFileSync(headerManifest, 'utf8').split(/\r?\n/)) {
      if (line.startsWith('runtime=') && fs.existsSync(line.slice('runtime='.length))) runtimePrelude = line.slice('runtime='.length)
    }
  }
  if (!runtimePrelude && fs.existsSync(path.join(generatedDir, 'gea_runtime.h'))) runtimePrelude = path.join(generatedDir, 'gea_runtime.h')
  if (runtimePrelude) {
    cxxSources.push(storageBridge)
    perUnitFlags.set(path.resolve(storageBridge), [`/DGEA_WINDOWS_RUNTIME_PRELUDE="${runtimePrelude.replace(/\\/g, '/')}"`])
  }
}

// A program using parallel regions says so in its runtime header
// (`#define GEA_RUNTIME_PARALLEL 1`). Every unit that sees the compiler runtime
// must agree on it -- the runtime's inline allocator and reference counting
// differ with parallel regions, and the linker keeps one unit's copy of each
// inline function for all -- and headers included before that line (the
// framework's, a package host's) see the runtime too, so it is build-wide.
const generatedParallelRuntime =
  fs.existsSync(generatedDir) &&
  fs
    .readdirSync(generatedDir)
    .filter((name) => name.endsWith('.runtime.hpp'))
    .some((name) => /^#define GEA_RUNTIME_PARALLEL 1\b/m.test(fs.readFileSync(path.join(generatedDir, name), 'utf8')))

const generatedSet = new Set(generatedSources.map((file) => path.resolve(file)))
const isGenerated = (file) => generatedSet.has(path.resolve(file)) || path.resolve(file).startsWith(path.resolve(generatedDir))

// --- compile ---------------------------------------------------------------------

const debug = flag('--debug')
const optimization = process.env.GEA_WINDOWS_OPT ?? (debug ? '/Od' : '/O2')

// Typed native settings (gea.build.native / gea.targets.windows.build.native),
// resolved by the CLI into the build config:
//   jobs                parallel compiles (0: automatic; --jobs and GEA_WINDOWS_JOBS win)
//   arch                'default' | 'AVX2' | 'AVX512' instruction set (/arch)
//   debugInfo           CodeView debug information in optimized code (/Z7, /debug)
//   lto                 'off' | 'thin' (ThinLTO through lld-link)
//   ltoJobs, ltoCache   ThinLTO backend threads (0: linker default) and cache directory
//   ltoExcludeSources   sources built as ordinary objects under ThinLTO (a pattern)
//   unoptimizedSources  sources built at /Od, e.g. very large constant-data units (a pattern)
//   pgo, pgoProfile     'off' | 'generate' (instrumented) | 'use' (with a merged .profdata)
//   package             'none' | 'zip' (a self-contained archive beside the output folder)
// Patterns are regular expressions over a source's path with forward slashes.
const nativeSettings = {
  jobs: 0,
  arch: 'default',
  debugInfo: false,
  lto: 'off',
  ltoJobs: 0,
  ltoCache: '',
  ltoExcludeSources: '',
  unoptimizedSources: '',
  pgo: 'off',
  pgoProfile: '',
  package: 'none',
  ...(buildConfig.settings?.native ?? {}),
}
const sourcePattern = (pattern, name) => {
  if (!pattern) return null
  try {
    return new RegExp(pattern)
  } catch (error) {
    return fail(`native.${name} is not a regular expression: ${error.message}`)
  }
}
const ltoExcluded = sourcePattern(nativeSettings.ltoExcludeSources, 'ltoExcludeSources')
const unoptimized = sourcePattern(nativeSettings.unoptimizedSources, 'unoptimizedSources')
const appPath = (value, name) => {
  if (!value) return ''
  if (!appDir) fail(`native.${name} needs an app`)
  return path.resolve(appDir, value)
}
const pgoProfile = nativeSettings.pgo === 'use' ? appPath(nativeSettings.pgoProfile, 'pgoProfile') : ''
if (nativeSettings.pgo === 'use' && (!pgoProfile || !fs.existsSync(pgoProfile))) {
  fail(`native.pgo is 'use' but native.pgoProfile ${pgoProfile ? `names a missing file: ${pgoProfile}` : 'is not set'}; merge a profile with llvm-profdata first`)
}
const ltoCache = nativeSettings.lto === 'thin' ? appPath(nativeSettings.ltoCache, 'ltoCache') : ''
const debugInfo = debug || nativeSettings.debugInfo === true
const toolchainFlags = [
  ...(nativeSettings.arch !== 'default' ? [`/arch:${nativeSettings.arch}`] : []),
  ...(nativeSettings.lto === 'thin' ? ['-flto=thin'] : []),
  ...(nativeSettings.pgo === 'generate' ? ['-fprofile-instr-generate'] : []),
  ...(pgoProfile ? [`-fprofile-instr-use=${pgoProfile}`, '-Wno-profile-instr-out-of-date', '-Wno-profile-instr-unprofiled'] : []),
]
const defines = [
  '/DNOMINMAX',
  '/DUNICODE',
  '/D_UNICODE',
  '/DWIN32_LEAN_AND_MEAN',
  '/D_CRT_SECURE_NO_WARNINGS',
  '/D_USE_MATH_DEFINES',
  '/DGEA_EMBEDDED_ENABLE_VIRTUAL_KEYBOARD=0',
  '/DGEA_EMBEDDED_TTF_RUNTIME_FONTS=1',
  `/DGEA_WINDOWS_APP_ID="${appId}"`,
  `/DGEA_WINDOWS_APP_NAME="${appName.replace(/"/g, '')}"`,
  // Program-defined hooks are ordinary references with linked defaults (see the
  // COFF note where the source lists are assembled).
  '/DGEA_LINKED_HOOK_DEFAULTS=1',
  ...(generatedParallelRuntime ? ['/DGEA_RUNTIME_PARALLEL=1'] : []),
]
defines.push(...nativeDefineFlags({ nativePackages, runtimeProviders }))
// The app's gea.defines, as resolved by the CLI, apply to the whole build.
for (const define of buildConfig.defines ?? appJson?.defines ?? []) {
  if (typeof define === 'string' && define) defines.push(`/D${define}`)
}
// -fverify-intermediate-code runs LLVM's IR verifier, which clang skips by
// default. clang 22 and 23 targeting MSVC can produce invalid IR at -O1 and
// above for a coroutine whose catch handler assigns a local declared outside
// the try (CoroSplit: "Instruction does not dominate all uses" in a catch
// funclet); unverified, that build succeeds and the app runs miscompiled.
// Verified, it fails with "Broken function found". About 1% of compile time,
// nothing at run time; an interim safety net until the emitter avoids the
// trigger or LLVM is fixed.
const commonFlags = [
  '/nologo',
  '/c',
  optimization,
  '/EHsc',
  '/MD',
  '/bigobj',
  '/utf-8',
  '/w',
  '-fverify-intermediate-code',
  ...(debugInfo ? ['/Z7'] : []),
  ...toolchainFlags,
  ...defines,
]
const cxxFlags = ['/std:c++20', '/Zc:__cplusplus', '/permissive-', ...commonFlags]
const cFlags = ['/DGEA_EMBEDDED_GIF_C_API', ...commonFlags]
const includeFlags = includeDirs.map((dir) => `/I${dir}`)

function objectPathFor(source, suffix) {
  const relativeRoots = [targetDir, generatedDir, geaCore, geaHost, geaEngine, geaElements, geaGeaos, appDir].filter(Boolean)
  let relative = source
  for (const root of relativeRoots) {
    if (path.resolve(source).startsWith(path.resolve(root))) {
      relative = `${path.basename(root)}__${path.relative(root, source)}`
      break
    }
  }
  const mangled = relative.replace(/[\\/:]+/g, '__').replace(/\.(?:cpp|cc|cxx|c|mm|m)$/, '')
  return path.join(buildDir, `${mangled}${suffix}.obj`)
}

function hashText(text) {
  return crypto.createHash('sha256').update(text).digest('hex')
}

const clangVersion = spawnSync(clangCl, ['--version'], { encoding: 'utf8' }).stdout.split(/\r?\n/)[0] ?? ''

function depsAreOlder(depfile, object) {
  if (!fs.existsSync(depfile)) return false
  const objectTime = fs.statSync(object).mtimeMs
  const text = fs.readFileSync(depfile, 'utf8').replace(/\\\r?\n/g, ' ')
  // The target separator is the first colon followed by whitespace; a drive
  // letter colon (C:\...) is followed by a backslash.
  const colon = text.search(/:(?=\s)/)
  const deps = (colon >= 0 ? text.slice(colon + 1) : text)
    .split(/(?<!\\) /)
    .map((entry) => entry.trim().replace(/\\ /g, ' '))
    .filter(Boolean)
  for (const dep of deps) {
    if (!fs.existsSync(dep)) return false
    if (fs.statSync(dep).mtimeMs > objectTime) return false
  }
  return true
}

function needsCompile(source, object, signature) {
  if (!fs.existsSync(object)) return true
  const signatureFile = `${object}.sig`
  if (!fs.existsSync(signatureFile) || fs.readFileSync(signatureFile, 'utf8') !== signature) return true
  if (fs.statSync(source).mtimeMs > fs.statSync(object).mtimeMs) return true
  return !depsAreOlder(`${object}.d`, object)
}

const jobs = Math.max(
  1,
  Number.parseInt(option('--jobs', process.env.GEA_WINDOWS_JOBS ?? ''), 10) ||
    nativeSettings.jobs ||
    Math.min(os.availableParallelism?.() ?? os.cpus().length, 12),
)

// Per-source exceptions to the build-wide flags (later flags win in clang-cl).
for (const source of [...cSources, ...cxxSources]) {
  const forward = path.resolve(source).replace(/\\/g, '/')
  const extra = []
  if (unoptimized?.test(forward)) extra.push('/Od')
  if (ltoExcluded?.test(forward)) extra.push('-fno-lto')
  if (extra.length) perUnitFlags.set(path.resolve(source), [...(perUnitFlags.get(path.resolve(source)) ?? []), ...extra])
}

const compileUnits = []
for (const source of cSources) {
  const object = objectPathFor(source, '.c')
  compileUnits.push({ source, object, flags: cFlags, kind: 'c' })
}
for (const source of cxxSources) {
  const object = objectPathFor(source, '.cxx')
  compileUnits.push({ source, object, flags: cxxFlags, kind: 'cxx' })
}
const objects = compileUnits.map((unit) => unit.object)

function compileCommand(unit) {
  const depfile = `${unit.object}.d`
  const commandArgs = [
    ...unit.flags,
    ...(perUnitFlags.get(path.resolve(unit.source)) ?? []),
    ...includeFlags,
    unit.kind === 'c' ? '/TC' : '/TP',
    '/clang:-MD',
    `/clang:-MF${depfile}`,
    `/Fo${unit.object}`,
    unit.source,
  ]
  return commandArgs
}

async function runCompiles() {
  const pending = compileUnits.filter((unit) => {
    const signature = hashText(`${clangVersion}\n${compileCommand(unit).slice(0, -2).join('\n')}`)
    unit.signature = signature
    return needsCompile(unit.source, unit.object, signature)
  })
  if (pending.length === 0) return 0
  process.stdout.write(`Compiling ${pending.length} of ${compileUnits.length} translation units (jobs=${jobs})...\n`)
  // Biggest sources first keeps the pool busy at the tail.
  pending.sort((a, b) => fs.statSync(b.source).size - fs.statSync(a.source).size)
  let index = 0
  let failed = false
  const failures = []
  const worker = async () => {
    while (index < pending.length && !failed) {
      const unit = pending[index++]
      fs.rmSync(`${unit.object}.sig`, { force: true })
      const commandArgs = compileCommand(unit)
      if (verbose) process.stdout.write(`${path.basename(clangCl)} ${commandArgs.join(' ')}\n`)
      else process.stdout.write(`  CXX ${path.relative(projectDir, unit.source).replace(/\\/g, '/')}\n`)
      const result = await new Promise((resolve) => {
        const child = spawn(clangCl, commandArgs, { stdio: ['ignore', 'pipe', 'pipe'] })
        let output = ''
        child.stdout.on('data', (chunk) => (output += chunk))
        child.stderr.on('data', (chunk) => (output += chunk))
        child.on('close', (code) => resolve({ code, output }))
        child.on('error', (error) => resolve({ code: 1, output: String(error) }))
      })
      if (result.code !== 0) {
        failed = true
        failures.push(`${unit.source}\n${result.output}`)
        fs.rmSync(unit.object, { force: true })
        return
      }
      if (result.output.trim()) process.stderr.write(result.output)
      fs.writeFileSync(`${unit.object}.sig`, unit.signature)
    }
  }
  await Promise.all(Array.from({ length: Math.min(jobs, pending.length) }, worker))
  if (failed) {
    process.stderr.write(failures.join('\n'))
    fail('compilation failed')
  }
  return pending.length
}

const compiled = await runCompiles()
mark('compile')

// --- resources: icon + manifest --------------------------------------------------------

function buildIco(pngFiles) {
  // An ICO container of PNG-encoded images (Vista+), one entry per size.
  const entries = pngFiles
    .map((file) => {
      const data = fs.readFileSync(file)
      if (data.length < 24 || data.readUInt32BE(0) !== 0x89504e47) return null
      const width = data.readUInt32BE(16)
      const height = data.readUInt32BE(20)
      if (width > 256 || height > 256) return null
      return { data, width, height }
    })
    .filter(Boolean)
    .sort((a, b) => a.width - b.width)
  if (entries.length === 0) return null
  const header = Buffer.alloc(6)
  header.writeUInt16LE(0, 0)
  header.writeUInt16LE(1, 2)
  header.writeUInt16LE(entries.length, 4)
  const directory = Buffer.alloc(16 * entries.length)
  let offset = header.length + directory.length
  entries.forEach((entry, index) => {
    const at = index * 16
    directory.writeUInt8(entry.width === 256 ? 0 : entry.width, at)
    directory.writeUInt8(entry.height === 256 ? 0 : entry.height, at + 1)
    directory.writeUInt8(0, at + 2)
    directory.writeUInt8(0, at + 3)
    directory.writeUInt16LE(1, at + 4)
    directory.writeUInt16LE(32, at + 6)
    directory.writeUInt32LE(entry.data.length, at + 8)
    directory.writeUInt32LE(offset, at + 12)
    offset += entry.data.length
  })
  return Buffer.concat([header, directory, ...entries.map((entry) => entry.data)])
}

const manifestText = fs
  .readFileSync(path.join(targetDir, 'app.manifest.in'), 'utf8')
  .replace(/@APP_ID@/g, appId)
  .replace(/@APP_NAME@/g, appName.replace(/[<>&]/g, ''))
const manifestPath = path.join(buildDir, 'app.manifest')
if (!fs.existsSync(manifestPath) || fs.readFileSync(manifestPath, 'utf8') !== manifestText) fs.writeFileSync(manifestPath, manifestText)

let resourceObject = ''
{
  const icons = appJson?.icons ? Object.values(appJson.icons) : []
  const iconFiles = icons.map((icon) => (path.isAbsolute(icon) ? icon : path.join(appDir, icon))).filter((file) => fs.existsSync(file))
  const ico = buildIco(iconFiles)
  const icoPath = path.join(buildDir, 'app.ico')
  if (ico) fs.writeFileSync(icoPath, ico)
  if (llvmRc) {
    const rcText = [`#include <winuser.h>`, ico ? `1 ICON "app.ico"` : '', `1 RT_MANIFEST "app.manifest"`].filter(Boolean).join('\n') + '\n'
    const rcPath = path.join(buildDir, 'app.rc')
    const resPath = path.join(buildDir, 'app.res')
    const rcSignature = hashText(rcText + manifestText + (ico ? ico.toString('base64') : ''))
    const rcSignaturePath = `${resPath}.sig`
    if (!fs.existsSync(resPath) || !fs.existsSync(rcSignaturePath) || fs.readFileSync(rcSignaturePath, 'utf8') !== rcSignature) {
      fs.writeFileSync(rcPath, rcText)
      const includeDir = findWindowsSdkInclude()
      const rcArgs = [...(includeDir ? [`/I${path.join(includeDir, 'um')}`, `/I${path.join(includeDir, 'shared')}`] : []), '/FO', resPath, rcPath]
      const result = spawnSync(llvmRc, rcArgs, { encoding: 'utf8', cwd: buildDir })
      if (result.status !== 0) {
        process.stderr.write(`warning: resource compilation failed, continuing without an icon:\n${result.stdout}${result.stderr}`)
        fs.rmSync(resPath, { force: true })
      } else {
        fs.writeFileSync(rcSignaturePath, rcSignature)
      }
    }
    if (fs.existsSync(resPath)) resourceObject = resPath
  }
}

function findWindowsSdkInclude() {
  const kits = path.join(process.env['ProgramFiles(x86)'] ?? 'C:/Program Files (x86)', 'Windows Kits/10/Include')
  if (!fs.existsSync(kits)) return ''
  const versions = fs
    .readdirSync(kits)
    .filter((name) => /^\d+\.\d+\.\d+\.\d+$/.test(name) && fs.existsSync(path.join(kits, name, 'um', 'winuser.h')))
    .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))
  return versions.length > 0 ? path.join(kits, versions[0]) : ''
}

// --- link ------------------------------------------------------------------------

const libraries = [
  'user32.lib', 'gdi32.lib', 'comctl32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'uxtheme.lib', 'dwmapi.lib',
  'shcore.lib', 'gdiplus.lib', 'msimg32.lib', 'winhttp.lib', 'iphlpapi.lib', 'ws2_32.lib', 'advapi32.lib', 'comdlg32.lib',
  'xaudio2.lib', 'mfplat.lib', 'mfreadwrite.lib', 'mfuuid.lib', 'shlwapi.lib', 'winmm.lib',
  ...nativePackages.flatMap((entry) => [...entry.libraries, ...entry.systemLibraries]),
  ...(appNativeLink.nativeSystemLibraries ?? []).map((name) => (/\.lib$/i.test(name) ? name : `${name}.lib`)),
  ...(nativeSettings.pgo === 'generate' ? [profileRuntime()] : []),
]

// clang's profile runtime for an instrumented (native.pgo 'generate') build.
function profileRuntime() {
  const clangLib = path.join(llvmBin, '..', 'lib', 'clang')
  const versions = fs.existsSync(clangLib) ? fs.readdirSync(clangLib).sort((a, b) => b.localeCompare(a, undefined, { numeric: true })) : []
  for (const version of versions) {
    const candidate = path.join(clangLib, version, 'lib', 'windows', 'clang_rt.profile-x86_64.lib')
    if (fs.existsSync(candidate)) return candidate
  }
  return fail(`native.pgo 'generate' needs clang's profile runtime (clang_rt.profile-x86_64.lib) under ${clangLib}`)
}
const linkArgs = [
  '/nologo',
  `/out:${executable}`,
  '/subsystem:windows',
  '/entry:wWinMainCRTStartup',
  // An executable that exports symbols (a D3D12 Agility SDK version, say)
  // needs no import library beside it.
  '/noimplib',
  // Identical-code folding stays on (lld-link's default without /debug, and
  // stated with it): the Gea runtime keeps no fact about a callable behind its
  // entry's address.
  ...(debugInfo ? ['/debug', '/opt:ref', '/opt:icf'] : []),
  ...(nativeSettings.lto === 'thin' && nativeSettings.ltoJobs > 0 ? [`/opt:lldltojobs=${nativeSettings.ltoJobs}`] : []),
  ...(ltoCache ? [`/lldltocache:${ltoCache}`] : []),
  ...(resourceObject ? [resourceObject] : ['/manifest:embed', `/manifestinput:${manifestPath}`]),
  ...objects,
  ...libraries,
]
const linkSignature = hashText(linkArgs.join('\n'))
const linkSignaturePath = path.join(buildDir, 'link.sig')
let linkNeeded = compiled > 0 || !fs.existsSync(executable) || !fs.existsSync(linkSignaturePath) || fs.readFileSync(linkSignaturePath, 'utf8') !== linkSignature
if (!linkNeeded) {
  const exeTime = fs.statSync(executable).mtimeMs
  linkNeeded = [...objects, resourceObject].filter(Boolean).some((object) => fs.statSync(object).mtimeMs > exeTime)
}
if (linkNeeded) {
  process.stdout.write(`Linking ${path.relative(projectDir, executable)}...\n`)
  fs.rmSync(linkSignaturePath, { force: true })
  const responseFile = path.join(buildDir, 'link.rsp')
  fs.writeFileSync(responseFile, linkArgs.map((argument) => `"${argument.replace(/\\/g, '\\\\')}"`).join('\n'))
  const result = spawnSync(lldLink, [`@${responseFile}`], { encoding: 'utf8' })
  if (result.status !== 0) {
    process.stderr.write(result.stdout + result.stderr)
    fail('link failed')
  }
  const linkOutput = (result.stdout + result.stderr).trim()
  if (linkOutput) process.stderr.write(`${linkOutput}\n`)
  fs.writeFileSync(linkSignaturePath, linkSignature)
}
mark('link')

// --- resources: window config, fonts, sounds -------------------------------------------

function copyIfChanged(from, to) {
  const data = fs.readFileSync(from)
  if (fs.existsSync(to) && Buffer.compare(fs.readFileSync(to), data) === 0) return false
  fs.mkdirSync(path.dirname(to), { recursive: true })
  fs.writeFileSync(to, data)
  return true
}

// Native runtime files (a DLL the executable loads at startup) go beside the
// executable, not into Resources: Windows resolves an import library's DLL
// from the executable's own directory first.
let stagingPlan = []
try {
  stagingPlan = runtimeStagingPlan({ nativePackages, runtimeProviders })
} catch (error) {
  if (error instanceof NativeBuildError) fail(error.message)
  throw error
}
for (const file of stagingPlan) copyIfChanged(file.from, path.join(distDir, ...file.to.split('/')))

function copyTreeIfChanged(from, to) {
  for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
    const source = path.join(from, entry.name)
    if (entry.isDirectory()) copyTreeIfChanged(source, path.join(to, entry.name))
    else if (entry.isFile()) copyIfChanged(source, path.join(to, entry.name))
  }
}
// gea.targets.windows.nativeResources: { "<name>": "<app directory>" } lands in Resources/<name>.
for (const [name, source] of Object.entries(appJson?.nativeResources?.windows ?? {})) {
  if (fs.existsSync(source) && fs.statSync(source).isDirectory()) copyTreeIfChanged(source, path.join(resourcesDir, name))
}

if (appDir) {
  // Window chrome config: a Windows-specific windows.json, else the macos.json
  // the same app ships for its Mac build (same schema).
  const config = ['windows.json', 'macos.json'].map((name) => path.join(appDir, name)).find((file) => fs.existsSync(file))
  const windowJson = path.join(resourcesDir, 'window.json')
  if (config) copyIfChanged(config, windowJson)
  else fs.rmSync(windowJson, { force: true })

  // Fonts referenced by @font-face in the app's CSS.
  const fontsDir = path.join(resourcesDir, 'Fonts')
  const wanted = new Set()
  const stack = [appDir]
  while (stack.length > 0) {
    const current = stack.pop()
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const full = path.join(current, entry.name)
      if (entry.isDirectory()) {
        if (!['node_modules', 'dist', 'build'].includes(entry.name)) stack.push(full)
        continue
      }
      if (!entry.isFile() || !entry.name.endsWith('.css')) continue
      const css = fs.readFileSync(full, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '')
      const blocks = css.matchAll(/@font-face\s*\{([^}]+)\}/gi)
      for (const block of blocks) {
        const source = block[1].match(/src\s*:\s*url\(\s*(?:"([^"]+)"|'([^']+)'|([^)"']+))\s*\)/i)
        if (!source) continue
        const raw = (source[1] || source[2] || source[3] || '').trim()
        if (!raw || /^https?:\/\//i.test(raw) || raw.startsWith('data:')) continue
        const resolved = path.resolve(path.dirname(full), raw.split(/[?#]/, 1)[0])
        if (fs.existsSync(resolved)) wanted.add(resolved)
      }
    }
  }
  if (wanted.size > 0) fs.mkdirSync(fontsDir, { recursive: true })
  for (const font of wanted) copyIfChanged(font, path.join(fontsDir, path.basename(font)))

  const soundsSource = path.join(appDir, 'src', 'sounds')
  if (fs.existsSync(soundsSource)) {
    const soundsDir = path.join(resourcesDir, 'Sounds')
    for (const name of fs.readdirSync(soundsSource)) {
      if (/\.(?:mp3|wav|ogg|m4a)$/i.test(name)) copyIfChanged(path.join(soundsSource, name), path.join(soundsDir, name))
    }
  }
}
mark('resources')

// native.package 'zip': a self-contained archive of the executable, its
// runtime files and Resources, beside the output folder.
if (nativeSettings.package === 'zip') {
  const archive = path.join(outputRoot, `${exeBaseName}-Windows.zip`)
  fs.rmSync(archive, { force: true })
  const tar = firstExisting([path.join(process.env.SystemRoot ?? 'C:/Windows', 'System32', 'tar.exe')]) || onPath('tar.exe')
  if (!tar) fail('native.package zip needs tar.exe (part of Windows 10 and later)')
  // The archive holds one folder named after the executable: a temporary copy
  // of the executable, its runtime files and Resources.
  const stageRoot = path.join(outputRoot, '.package', appId)
  const staged = path.join(stageRoot, exeBaseName)
  fs.rmSync(stageRoot, { recursive: true, force: true })
  // Only what the build put there: files a run writes beside the executable
  // (logs, settings, caches) stay out.
  const shipped = [path.basename(executable), 'Resources', ...stagingPlan.map((file) => file.to)]
  for (const name of shipped) {
    const from = path.join(distDir, ...name.split('/'))
    if (!fs.existsSync(from)) continue
    if (fs.statSync(from).isDirectory()) copyTreeIfChanged(from, path.join(staged, ...name.split('/')))
    else copyIfChanged(from, path.join(staged, ...name.split('/')))
  }
  const result = spawnSync(tar, ['-a', '-c', '-f', archive, exeBaseName], { cwd: stageRoot, encoding: 'utf8' })
  fs.rmSync(stageRoot, { recursive: true, force: true })
  if (result.status !== 0) fail(`packaging failed:\n${result.stdout}${result.stderr}`)
  process.stdout.write(`Packaged ${archive}\n`)
}
mark('package')

process.stdout.write(`Built ${executable}\n`)
if (process.env.GEA_WINDOWS_TIMINGS === '1') {
  for (const [label, ms] of timings) process.stderr.write(`[windows timing] ${label.padEnd(12)} ${(ms / 1000).toFixed(2)}s\n`)
}
if (flag('--run')) {
  process.stdout.write(`Running ${executable}\n`)
  const child = spawn(executable, [], { stdio: 'inherit', cwd: distDir, env: process.env })
  await new Promise((resolve) => child.on('close', resolve))
} else {
  process.stdout.write(`Run: "${executable}"\n`)
}
