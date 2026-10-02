import assert from 'node:assert/strict'
import fs from 'node:fs'
import test from 'node:test'
import ts from 'typescript'

test('the published plugin owns a bounded TypeScript compiler API dependency', () => {
  const manifest = JSON.parse(fs.readFileSync(new URL('../package.json', import.meta.url), 'utf8'))
  assert.equal(manifest.dependencies.typescript, '^5.9.3')
  assert.equal(manifest.peerDependencies.typescript, undefined)
  assert.equal(manifest.devDependencies.typescript, undefined)
  const source = ts.createSourceFile('app.ts', 'const value = 1', ts.ScriptTarget.Latest, true)
  assert.equal(source.parseDiagnostics.length, 0)
})
