#!/usr/bin/env node
// Regenerates generated/*.d.ts and runtime/*.js from the SDK fixture in
// dist/index.js. Run after `tsc` (npm run build does both).
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const packageRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const sdkModule = path.join(packageRoot, 'dist/index.js')
if (!fs.existsSync(sdkModule)) {
  process.stderr.write(`missing ${sdkModule}; run tsc first\n`)
  process.exit(1)
}
const { windowsSdkFixture, generateWindowsDeclarations, generateWindowsRuntimeModules } = await import(pathToFileURL(sdkModule).href)

function writeIfChanged(file, contents) {
  fs.mkdirSync(path.dirname(file), { recursive: true })
  if (fs.existsSync(file) && fs.readFileSync(file, 'utf8') === contents) return false
  fs.writeFileSync(file, contents)
  return true
}

let changed = 0
for (const [specifier, source] of Object.entries(generateWindowsDeclarations(windowsSdkFixture))) {
  const name = specifier.slice('@geastack/windows/'.length)
  if (writeIfChanged(path.join(packageRoot, 'generated', `${name}.d.ts`), source)) changed++
}
for (const [specifier, source] of Object.entries(generateWindowsRuntimeModules(windowsSdkFixture))) {
  const name = specifier.slice('@geastack/windows/'.length)
  if (writeIfChanged(path.join(packageRoot, 'runtime', `${name}.js`), source)) changed++
}
process.stdout.write(`generated ${changed} changed file(s)\n`)
