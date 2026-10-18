// Native code and runtime files that packages and the app contribute to a
// Windows build. build-windows.mjs calls these; they read the file system but
// compile nothing, so the selection rules are testable on their own.
//
// A package that carries native code for this target says so in its own
// `gea-native.json`, and the build picks it up without the app repeating any
// of it: a WebGPU or WebGL host compiled into the program, a header on the
// include path, an import library linked, a DLL beside the executable.
//
//   { "windows": {
//       "sources": [...],          .c, .cpp/.cc/.cxx, and .mm (compiled as C++)
//       "includeDirs": [...], "libraries": [...], "systemLibraries": ["name.lib"],
//       "runtimeFiles": [...], "defines": { "NAME": "value" },
//       "requires": { "<capability>": { "description": "...",
//                       "files": { "<role>": { "define": "MACRO", "default": "file.dll" } } } },
//       "provides": { "<capability>": { "directory": ".", "files": { "<role>": "file.dll" },
//                       "runtimeFiles": [...], "defines": {...} } } } }
//
// Paths are relative to the package root. Only the packages the program
// actually uses count: those owning a module of the compiled module graph
// (the app's declared dependencies when the build wrote no graph). A package
// whose file names something missing is reported rather than silently
// dropped: the usual cause is a vendored binary that was never fetched, and a
// link error naming an unresolved symbol is a worse way to find that out.
//
// `requires` names a runtime capability a package needs but does not ship,
// such as an EGL/OpenGL ES implementation. The app chooses the implementation
// in gea.targets.windows.providers.<capability>; the build stages its files
// beside the executable and tells the package, through each role's define,
// which file implements that role. A required capability nobody provides
// fails the build, not the program at launch.

import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'

export class NativeBuildError extends Error {}

function fail(message) {
  throw new NativeBuildError(message)
}

// Sources this target compiles. .m is C here and .mm C++: on Windows those
// sources carry their Objective-C behind __APPLE__ conditionals.
export const nativeSourcePattern = /\.(?:c|cc|cpp|cxx|mm|m)$/
export function nativeSourceLanguage(file) {
  return /\.(?:c|m)$/.test(file) ? 'c' : 'cxx'
}

function readJsonFile(file, what) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'))
  } catch (error) {
    return fail(`${what} is not readable JSON: ${error.message}`)
  }
}

function packageNameAt(dir) {
  try {
    return JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8')).name ?? ''
  } catch {
    return ''
  }
}

// The nearest enclosing directory whose package.json names a package (a
// nested {"type": "module"} manifest is not a package boundary).
function makePackageRootOf() {
  const cache = new Map()
  return (file) => {
    let dir = path.dirname(file)
    const visited = []
    let found = ''
    for (;;) {
      if (cache.has(dir)) {
        found = cache.get(dir)
        break
      }
      visited.push(dir)
      if (fs.existsSync(path.join(dir, 'package.json')) && packageNameAt(dir)) {
        found = dir
        break
      }
      const parent = path.dirname(dir)
      if (parent === dir) break
      dir = parent
    }
    for (const entry of visited) cache.set(entry, found)
    return found
  }
}

// Package roots owning a module of the compiled program, or null when the
// build wrote no (readable) module graph.
export function usedPackageRoots(moduleGraphPath) {
  if (!moduleGraphPath || !fs.existsSync(moduleGraphPath)) return null
  let graph
  try {
    graph = JSON.parse(fs.readFileSync(moduleGraphPath, 'utf8'))
  } catch {
    return null
  }
  if (!Array.isArray(graph.modules)) return null
  const packageRootOf = makePackageRootOf()
  const roots = new Set()
  for (const module of graph.modules) {
    const file = module?.file
    if (typeof file !== 'string' || !path.isAbsolute(file) || !fs.existsSync(file)) continue
    const root = packageRootOf(file)
    if (root) roots.add(fs.realpathSync(root))
  }
  return roots
}

function resolvePackageRoot(appDir, name) {
  const appRequire = createRequire(path.join(appDir, 'package.json'))
  for (const request of [`${name}/gea-native.json`, `${name}/package.json`]) {
    try {
      return fs.realpathSync(path.dirname(appRequire.resolve(request)))
    } catch {
      // Not exported under that name; try the next.
    }
  }
  return ''
}

export function declaredDependencyRoots(appDir) {
  const roots = new Set()
  let manifest
  try {
    manifest = JSON.parse(fs.readFileSync(path.join(appDir, 'package.json'), 'utf8'))
  } catch {
    return roots
  }
  for (const name of [...Object.keys(manifest.dependencies ?? {}), ...Object.keys(manifest.devDependencies ?? {})]) {
    const root = resolvePackageRoot(appDir, name)
    if (root) roots.add(root)
  }
  return roots
}

function windowsSection(descriptor) {
  return descriptor.windows && typeof descriptor.windows === 'object' ? descriptor.windows : descriptor
}

// The native contributions of the packages the program uses.
export function readNativePackages({ appDir, moduleGraphPath }) {
  const found = []
  if (!appDir) return found
  const appRoot = fs.realpathSync(appDir)
  const roots = usedPackageRoots(moduleGraphPath) ?? declaredDependencyRoots(appDir)
  for (const root of [...roots].sort()) {
    if (root === appRoot) continue
    const descriptorPath = path.join(root, 'gea-native.json')
    if (!fs.existsSync(descriptorPath)) continue
    const name = packageNameAt(root) || path.basename(root)
    const section = windowsSection(readJsonFile(descriptorPath, `${name}'s gea-native.json`))
    const resolve = (entries, what) =>
      (entries ?? []).map((entry) => {
        const full = path.resolve(root, entry)
        if (!fs.existsSync(full)) {
          fail(`${name} names a ${what} its package does not contain: ${entry}\n` +
               `Run the package's fetch or build script (see its README) and build again.`)
        }
        return full
      })
    const sources = resolve(section.sources, 'source')
    for (const source of sources) {
      if (!nativeSourcePattern.test(source)) fail(`${name}'s gea-native.json names a source this target cannot compile: ${path.relative(root, source)}`)
    }
    found.push({
      name,
      root,
      sources,
      includeDirs: resolve(section.includeDirs, 'include directory'),
      libraries: resolve(section.libraries, 'library'),
      systemLibraries: section.systemLibraries ?? [],
      runtimeFiles: resolve(section.runtimeFiles, 'runtime file'),
      defines: section.defines ?? {},
      requires: section.requires ?? {},
    })
  }
  return found
}

function resolveProvider({ appDir, providers, capability, requirement, requiredBy }) {
  const where = `gea.targets.windows.providers.${capability}`
  const roles = Object.entries(requirement.files ?? {})
  const raw = (providers ?? {})[capability]
  if (raw === undefined || raw === null || raw === false) {
    const files = roles.map(([role, spec]) => `${role}${spec.default ? ` (default ${spec.default})` : ''}`).join(', ')
    return fail(
      `${requiredBy.join(', ')} ${requiredBy.length > 1 ? 'need' : 'needs'} a '${capability}' provider on Windows: ${requirement.description ?? capability}.\n` +
        `Declare one in the app's package.json as ${where}: a directory holding ${files ? `its files (${files})` : 'its files'}, ` +
        `an object { "directory", "files": { <role>: <file> }, "runtimeFiles": [...] }, ` +
        `or { "package": "<name>" } for a package whose gea-native.json provides '${capability}'.`,
    )
  }
  let spec = raw
  let base = appDir
  let origin = where
  if (typeof raw === 'string') {
    spec = { directory: raw }
  } else if (typeof raw !== 'object' || Array.isArray(raw)) {
    return fail(`${where} must be a directory or an object`)
  } else if (typeof raw.package === 'string' && raw.package) {
    const providerRoot = resolvePackageRoot(appDir, raw.package)
    if (!providerRoot || !fs.existsSync(path.join(providerRoot, 'gea-native.json'))) {
      return fail(`${where} names ${raw.package}, which is not installed in ${appDir} or ships no gea-native.json`)
    }
    const provided = windowsSection(readJsonFile(path.join(providerRoot, 'gea-native.json'), `${raw.package}'s gea-native.json`)).provides?.[capability]
    if (!provided) return fail(`${where} names ${raw.package}, whose gea-native.json does not provide '${capability}' for windows`)
    spec = provided
    base = providerRoot
    origin = `${raw.package}'s gea-native.json`
  }
  const directory = path.resolve(base, spec.directory ?? '.')
  if (!fs.existsSync(directory) || !fs.statSync(directory).isDirectory()) {
    return fail(`the '${capability}' provider directory (${origin}) does not exist: ${directory}\nBuild or fetch the provider first.`)
  }
  const fileIn = (entry, what) => {
    if (typeof entry !== 'string' || !entry || path.isAbsolute(entry)) fail(`${origin}: the ${what} must be a path relative to the provider directory: ${entry}`)
    const full = path.resolve(directory, entry)
    const relative = path.relative(directory, full)
    if (relative.startsWith('..') || path.isAbsolute(relative)) fail(`${origin}: the ${what} leaves the provider directory: ${entry}`)
    if (!fs.existsSync(full) || !fs.statSync(full).isFile()) {
      fail(`the '${capability}' provider (${origin}) has no ${what} ${relative} in ${directory}\nBuild or fetch the provider first.`)
    }
    return { full, relative: relative.split(path.sep).join('/') }
  }
  const files = roles.map(([role, roleSpec]) => {
    const entry = spec.files?.[role] ?? roleSpec.default
    if (!entry) fail(`${origin} names no '${role}' file for '${capability}', and ${requiredBy[0]} declares no default`)
    return { role, define: roleSpec.define ?? '', ...fileIn(entry, `${role} file`) }
  })
  const runtimeFiles = (spec.runtimeFiles ?? []).map((entry) => fileIn(entry, 'runtime file'))
  return { capability, directory, files, runtimeFiles, defines: spec.defines ?? {} }
}

// One provider per capability the used packages require.
export function resolveRuntimeProviders({ appDir, nativePackages, providers }) {
  const requirements = new Map()
  for (const entry of nativePackages) {
    for (const [capability, requirement] of Object.entries(entry.requires)) {
      const known = requirements.get(capability)
      if (known) known.requiredBy.push(entry.name)
      else requirements.set(capability, { requirement: requirement ?? {}, requiredBy: [entry.name] })
    }
  }
  return [...requirements].map(([capability, { requirement, requiredBy }]) =>
    resolveProvider({ appDir, providers, capability, requirement, requiredBy }))
}

// The preprocessor definitions packages and providers contribute, as clang-cl
// arguments. A provided file reaches the package that required it as a string
// literal naming the file relative to the executable's directory.
export function nativeDefineFlags({ nativePackages, runtimeProviders }) {
  const flags = []
  for (const entry of [...nativePackages, ...runtimeProviders]) {
    for (const [name, value] of Object.entries(entry.defines)) flags.push(value === true ? `/D${name}` : `/D${name}=${value}`)
  }
  for (const provider of runtimeProviders) {
    for (const file of provider.files) {
      if (file.define) flags.push(`/D${file.define}="${file.relative}"`)
    }
  }
  return flags
}

// Files to place beside the executable: { from, to } with `to` relative to its
// directory. A package's runtime files go there by name; a provider's keep
// their place relative to its directory, so a runtime that loads companions
// from a subdirectory of the executable's finds them.
export function runtimeStagingPlan({ nativePackages, runtimeProviders }) {
  const plan = []
  for (const entry of nativePackages) {
    for (const file of entry.runtimeFiles) plan.push({ from: file, to: path.basename(file) })
  }
  for (const provider of runtimeProviders) {
    for (const file of [...provider.files, ...provider.runtimeFiles]) plan.push({ from: file.full, to: file.relative })
  }
  const seen = new Map()
  return plan.filter((entry) => {
    const key = entry.to.toLowerCase()
    if (seen.has(key)) {
      if (path.resolve(seen.get(key)) !== path.resolve(entry.from)) fail(`two different files would be staged as ${entry.to}: ${seen.get(key)} and ${entry.from}`)
      return false
    }
    seen.set(key, entry.from)
    return true
  })
}
