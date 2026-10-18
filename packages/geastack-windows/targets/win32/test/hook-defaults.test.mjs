// Optional hooks on COFF: the target's linked defaults (main/win32_hook_defaults.cpp)
// apply when nothing defines a hook, a generated program's weak definition and
// an app's own definition replace them, and two strong definitions of one hook
// are still a duplicate-symbol link error. Windows only; needs clang-cl.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

const targetDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const fixtures = path.join(targetDir, 'test', 'fixtures', 'hook-defaults')

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

test('linked hook defaults yield to program and app definitions, and duplicates still fail', { skip: !clangCl || !fs.existsSync(lldLink) ? 'needs Windows with clang-cl and lld-link' : false }, (t) => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'gea-hook-defaults-'))
  t.after(() => fs.rmSync(out, { recursive: true, force: true }))
  const object = (source) => {
    const file = path.join(out, `${path.basename(source, '.cpp')}.obj`)
    const result = spawnSync(clangCl, ['/nologo', '/c', '/O2', '/MD', '/EHsc', '/std:c++20', '/DGEA_LINKED_HOOK_DEFAULTS=1', `/Fo${file}`, source], { encoding: 'utf8' })
    assert.equal(result.status, 0, result.stdout + result.stderr)
    return file
  }
  const defaults = object(path.join(targetDir, 'main', 'win32_hook_defaults.cpp'))
  const [framework, program, override, duplicate] = ['framework', 'program', 'app_override', 'app_duplicate'].map((name) => object(path.join(fixtures, `${name}.cpp`)))
  const link = (name, objects) => {
    const executable = path.join(out, `${name}.exe`)
    const result = spawnSync(lldLink, ['/nologo', `/out:${executable}`, '/subsystem:console', ...objects], { encoding: 'utf8' })
    if (result.status !== 0) return { linked: false, output: result.stdout + result.stderr }
    const run = spawnSync(executable, [], { encoding: 'utf8' })
    assert.equal(run.status, 0, run.stderr)
    return { linked: true, lines: run.stdout.split(/\r?\n/).filter(Boolean) }
  }
  assert.deepEqual(link('defaults', [framework, defaults]).lines, ['lookup 0 0'])
  assert.deepEqual(link('program', [framework, defaults, program]).lines, ['program defer', 'lookup 1 7'])
  assert.deepEqual(link('override', [framework, defaults, program, override]).lines, ['program defer', 'lookup 1 42'])
  const clash = link('duplicate', [framework, defaults, override, duplicate])
  assert.equal(clash.linked, false)
  assert.match(clash.output, /duplicate symbol: gea_embedded_asset_lookup/)
})
