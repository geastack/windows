// Declared native sources compile in the language their extension names and
// link with the target's host-extension registry: a package's .mm and .c
// sources and the registry (main/win32_host_extensions.cpp) are built the way
// build-windows.mjs builds them, and the extension a package registers from a
// static initializer is the one the target runs. Windows only; needs clang-cl.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

import { nativeSourceLanguage } from '../native-packages.mjs'

const targetDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const fixtures = path.join(targetDir, 'test', 'fixtures', 'host-extension')

function findClangCl() {
  if (process.platform !== 'win32') return ''
  if (process.env.GEA_WINDOWS_CLANG_CL) return process.env.GEA_WINDOWS_CLANG_CL
  const where = spawnSync('where', ['clang-cl.exe'], { encoding: 'utf8' })
  if (where.status === 0) return where.stdout.split(/\r?\n/).find(Boolean).trim()
  const vswhere = path.join(process.env['ProgramFiles(x86)'] ?? 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe')
  if (fs.existsSync(vswhere)) {
    const roots = spawnSync(vswhere, ['-all', '-products', '*', '-property', 'installationPath'], { encoding: 'utf8' }).stdout ?? ''
    for (const root of roots.split(/\r?\n/).map((line) => line.trim()).filter(Boolean)) {
      const candidate = path.join(root, 'VC/Tools/Llvm/x64/bin/clang-cl.exe')
      if (fs.existsSync(candidate)) return candidate
    }
  }
  const llvm = path.join(process.env.ProgramFiles ?? 'C:/Program Files', 'LLVM/bin/clang-cl.exe')
  return fs.existsSync(llvm) ? llvm : ''
}

const clangCl = findClangCl()
const lldLink = clangCl ? path.join(path.dirname(clangCl), 'lld-link.exe') : ''

test('package sources compile by language and their registered extension runs', { skip: !clangCl || !fs.existsSync(lldLink) ? 'needs Windows with clang-cl and lld-link' : false }, (t) => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'gea-host-extension-'))
  t.after(() => fs.rmSync(out, { recursive: true, force: true }))
  const sources = [
    path.join(fixtures, 'package_host.mm'),
    path.join(fixtures, 'package_support.c'),
    path.join(targetDir, 'main', 'win32_host_extensions.cpp'),
    path.join(fixtures, 'main.cpp'),
  ]
  const objects = sources.map((source, index) => {
    const object = path.join(out, `${index}.obj`)
    const language = nativeSourceLanguage(source)
    const args = [
      '/nologo', '/c', '/EHsc', '/MD', '/utf-8', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN',
      ...(language === 'c' ? ['/TC'] : ['/std:c++20', '/TP']),
      `/I${path.join(targetDir, 'include')}`, `/I${path.join(targetDir, 'main')}`, `/Fo${object}`, source,
    ]
    const result = spawnSync(clangCl, args, { encoding: 'utf8' })
    assert.equal(result.status, 0, `${path.basename(source)}:\n${result.stdout}${result.stderr}`)
    return object
  })
  const executable = path.join(out, 'host-extension.exe')
  const link = spawnSync(lldLink, ['/nologo', `/out:${executable}`, '/subsystem:console', ...objects], { encoding: 'utf8' })
  assert.equal(link.status, 0, link.stdout + link.stderr)
  const run = spawnSync(executable, [], { encoding: 'utf8' })
  assert.equal(run.status, 0, run.stderr)
  assert.deepEqual(run.stdout.split(/\r?\n/).filter(Boolean), ['extension fixture surface', 'attach 640x480 at 1.50', 'begin', 'end', 'pacing 1', 'support 42'])
  assert.match(run.stderr, /ignoring host extension future surface: interface version 2, this target implements 1/)
})
