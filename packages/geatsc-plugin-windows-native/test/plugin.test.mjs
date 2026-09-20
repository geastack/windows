// The compiler-facing contract of the Windows-native plugin: the host tables it
// derives from the SDK fixture, and the bridge artifacts it writes for a build
// that named its metadata.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import plugin, { createWindowsHostTables, windowsMetadataOption, windowsNativeBridgeHeader, windowsNativeBridgeSource } from '../dist/index.js'
import { generateWindowsBridgeMetadata, windowsSdkFixture } from '@geastack/windows'

const metadata = generateWindowsBridgeMetadata(windowsSdkFixture)

test('the host tables cover every library of the SDK fixture', () => {
  const tables = createWindowsHostTables(metadata)
  const keys = [...tables.hostMembers.keys()]
  assert.ok(tables.nativeTypes.size >= 40, `expected the Controls classes and Win32 structs, got ${tables.nativeTypes.size} native types`)
  assert.ok(keys.some((key) => key.endsWith('.addArrangedSubview')), 'WinStackView.addArrangedSubview is a host member')
  assert.ok(keys.some((key) => key.endsWith('.text')), 'WinLabel.text is a host property')
  assert.ok([...tables.hostFunctions.keys()].some((key) => key.includes('MessageBoxW')), 'User32.MessageBoxW is a host function')
  assert.ok([...tables.nativeConstants.keys()].some((key) => key.includes('MB_OK')), 'User32.MB_OK is a native constant')
  assert.ok(tables.hostConstructors.size >= 10, 'the constructible Controls classes have constructors')
  // Methods and properties call thunks in gea::windows; struct fields read
  // the receiver directly.
  const emits = [...tables.hostMembers.values()].map((member) => member.emit)
  assert.ok(emits.every((emit) => typeof emit === 'string' && emit.length > 0), 'every member has an emit template')
  assert.ok(emits.some((emit) => emit.includes('gea::windows::Controls::WinStackView_addArrangedSubview')), 'method members call their thunk')
  assert.ok(emits.some((emit) => emit === '{receiver}.x'), 'struct fields read the receiver')
})

test('without metadata the plugin declares the modules but writes no bridge', () => {
  const instance = plugin.instantiate(new Map())
  assert.equal(plugin.name, 'windows-native')
  assert.ok(instance.capabilities.declarationModules.has('@geastack/windows'))
  assert.ok(instance.capabilities.declarationModules.has('@geastack/windows/*'))
  assert.deepEqual(instance.capabilities.generatedSupportIncludes, [])
  assert.equal(instance.writeArtifacts, undefined)
})

test('with metadata the plugin writes the native bridge header and source', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gea-windows-plugin-'))
  try {
    const metadataFile = path.join(dir, 'metadata.json')
    fs.writeFileSync(metadataFile, JSON.stringify(metadata))
    const instance = plugin.instantiate(new Map([[windowsMetadataOption, metadataFile]]))
    assert.deepEqual(instance.capabilities.generatedSupportIncludes, [windowsNativeBridgeHeader])
    assert.equal(typeof instance.writeArtifacts, 'function')
    const outDir = path.join(dir, 'out')
    instance.writeArtifacts(outDir)
    const header = fs.readFileSync(path.join(outDir, windowsNativeBridgeHeader), 'utf8')
    const source = fs.readFileSync(path.join(outDir, windowsNativeBridgeSource), 'utf8')
    assert.ok(header.includes('GEA_HOST_DECLARED'), 'the header declares the engine the way the gea plugin does')
    assert.ok(header.includes('namespace gea::windows::handles'), 'the header carries the handle table')
    assert.ok(header.includes('WinLabel'), 'the header declares the Controls wrappers')
    assert.ok(header.includes('MessageBoxW'), 'the header declares the library thunks')
    assert.ok(source.includes(windowsNativeBridgeHeader), 'the source implements the header')
    // Writing again with identical content leaves the files untouched.
    const before = fs.statSync(path.join(outDir, windowsNativeBridgeHeader)).mtimeMs
    instance.writeArtifacts(outDir)
    assert.equal(fs.statSync(path.join(outDir, windowsNativeBridgeHeader)).mtimeMs, before)
  } finally {
    fs.rmSync(dir, { recursive: true, force: true })
  }
})
