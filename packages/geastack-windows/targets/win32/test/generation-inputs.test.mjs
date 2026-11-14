// A generating package swapped for another tree changes its identity even when
// every file of the new tree is older than the last generation.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import { generatorIdentity } from '../generation-inputs.mjs'

function packageTree(root, version, time) {
  fs.mkdirSync(path.join(root, 'dist'), { recursive: true })
  fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({ name: '@geastack/compiler', version }))
  const file = path.join(root, 'dist', 'cli.js')
  fs.writeFileSync(file, `// ${version}\n`)
  fs.utimesSync(file, time, time)
}

test('a swap to an older generator tree changes its identity', (t) => {
  const base = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'gea-generation-inputs-')))
  t.after(() => fs.rmSync(base, { recursive: true, force: true }))
  const published = path.join(base, 'published')
  const local = path.join(base, 'local')
  packageTree(published, '1.0.48', new Date('2026-11-10T12:00:00Z'))
  packageTree(local, '1.0.49', new Date('2026-11-01T12:00:00Z'))
  const spec = (root) => ({ name: 'compiler', root, dir: 'dist', files: /\.js$/ })

  const before = generatorIdentity(spec(published))
  assert.deepEqual(generatorIdentity(spec(published)), before, 'an unchanged tree keeps its identity')
  const swapped = generatorIdentity(spec(local))
  assert.ok(swapped.newest < before.newest, 'the swapped-in tree is older')
  assert.notDeepEqual(swapped, before)

  // The same version rebuilt in place: only the newest file time moves.
  fs.utimesSync(path.join(published, 'dist', 'cli.js'), new Date('2026-11-12T12:00:00Z'), new Date('2026-11-12T12:00:00Z'))
  assert.notEqual(generatorIdentity(spec(published)).newest, before.newest)
})

test('a junction is identified by the tree it points at', { skip: process.platform !== 'win32' }, (t) => {
  const base = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'gea-generation-inputs-')))
  const tree = path.join(base, 'tree')
  const link = path.join(base, 'link')
  // The junction goes first, so the cleanup never deletes through it.
  t.after(() => {
    if (fs.existsSync(link)) fs.unlinkSync(link)
    fs.rmSync(base, { recursive: true, force: true })
  })
  packageTree(tree, '1.0.49', new Date('2026-11-01T12:00:00Z'))
  fs.symlinkSync(tree, link, 'junction')
  assert.equal(generatorIdentity({ name: 'compiler', root: link, dir: 'dist', files: /\.js$/ }).root, tree)
})
