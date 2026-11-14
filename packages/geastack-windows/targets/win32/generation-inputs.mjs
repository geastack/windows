// What decides whether the program's C++ must be generated again: the newest
// time among its inputs, and the identity of every package that generates it.
import fs from 'node:fs'
import path from 'node:path'

/** The newest modification time of a file under `dir` that `predicate` accepts, skipping build and dependency directories. */
export function newestMtime(dir, predicate, skip = new Set(['node_modules', 'dist', 'build', '.vite', '.git'])) {
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

/** A package's version, or '' when its package.json cannot be read. */
export function packageVersionOf(root) {
  try {
    return String(JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')).version ?? '')
  } catch {
    return ''
  }
}

/**
 * One generating package as the last generation saw it: where it resolves (a
 * junction or symlink followed), its version, and the newest of its files
 * under `dir` that `files` matches.
 *
 * Comparing only "newer than the stamp" misses a swap to an OLDER tree: a
 * junction to a local compiler checkout built before the last generation, or
 * a switch back to the published package. The exact identity changes on any
 * swap, older or newer, so it belongs in the generation signature.
 */
export function generatorIdentity({ name, root, dir, files }) {
  return {
    name,
    root: fs.existsSync(root) ? fs.realpathSync(root) : root,
    version: packageVersionOf(root),
    newest: newestMtime(path.join(root, dir), (file) => files.test(file)),
  }
}
