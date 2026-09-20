import { mkdirSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'

// Vite plugins for a Windows-native Gea app (`gea.runtime: "windows-native"`).
//
// The native compiler (geatsc + @geastack/geatsc-plugin-windows-native) compiles
// the app's TypeScript sources, not the bundle Vite produces: the bundle exists
// only so Vite walks the module graph and the module-graph plugin from
// @geastack/core can snapshot every reachable source for the compiler. Three
// things have to hold for that walk to succeed:
//
//  1. JSX over the Controls classes (`<WinStackView spacing={8}/>`) has to be
//     ordinary construction before esbuild sees it, because no JSX factory is
//     configured for these apps. The compiler performs the same rewrite on the
//     sources it compiles; this one keeps the bundler happy.
//  2. The gea IR file the build pipeline expects has to exist; a natively
//     rendered app has no gea components, so it is written empty.
//  3. The module graph has to be recorded, which `geaModuleGraphPlugins` from
//     @geastack/core does when GEA_VITE_MODULE_GRAPH_OUT is set by the build.
//
// `windowsNativeVitePlugins()` returns all three, in the right order.

const requireBabel = createRequire(import.meta.url)
const { parse } = requireBabel('@babel/parser')
const traverseModule = requireBabel('@babel/traverse')
const t = requireBabel('@babel/types')
const generateModule = requireBabel('@babel/generator')
const traverse = traverseModule.default || traverseModule
const generate = generateModule.default || generateModule

// The Controls classes JSX may name. A tag outside this set is left alone.
const nativeViewTags = new Set([
  'WinView',
  'WinStackView',
  'WinLabel',
  'WinTextField',
  'WinTextView',
  'WinButton',
  'WinCheckBox',
  'WinSlider',
  'WinProgressBar',
  'WinImageView',
  'WinScrollView',
  'WinBox',
  'WinSplitView',
])

const arrangedSubviewTags = new Set(['WinStackView'])

export function windowsNativeJsxPlugin() {
  let nextId = 0
  return {
    name: 'gea-windows-native-jsx',
    enforce: 'pre',
    transform(code, id) {
      if (!id.endsWith('.tsx')) return null
      if (!/<Win[A-Z]/.test(code)) return null
      nextId = 0
      let ast
      try {
        ast = parse(code, { sourceType: 'module', plugins: ['jsx', 'typescript'] })
      } catch (error) {
        error.message = `${error.message} while parsing ${id}`
        throw error
      }
      let changed = false
      traverse(ast, {
        JSXElement(path) {
          const tag = jsxTagName(path.node.openingElement.name)
          if (!nativeViewTags.has(tag)) return
          path.replaceWith(buildNativeViewExpression(path.node, () => `__geaWinView${nextId++}`))
          path.skip()
          changed = true
        },
      })
      if (!changed) return null
      return { code: generate(ast, { comments: true }).code, map: null }
    },
  }
}

function buildNativeViewExpression(node, nextName) {
  const tag = jsxTagName(node.openingElement.name)
  if (!nativeViewTags.has(tag)) throw new Error(`Unsupported Windows native JSX tag <${tag}>`)
  const view = t.identifier(nextName())
  const statements = [t.variableDeclaration('const', [t.variableDeclarator(view, t.newExpression(t.identifier(tag), []))])]
  for (const attr of node.openingElement.attributes) {
    if (!t.isJSXAttribute(attr) || !t.isJSXIdentifier(attr.name)) {
      throw new Error(`Unsupported Windows native JSX attribute on <${tag}>`)
    }
    if (attr.name.name === 'children') continue
    statements.push(
      t.expressionStatement(t.assignmentExpression('=', t.memberExpression(view, t.identifier(attr.name.name)), jsxAttributeValue(attr.value))),
    )
  }
  const adder = arrangedSubviewTags.has(tag) ? 'addArrangedSubview' : 'addSubview'
  for (const child of node.children) {
    if (t.isJSXElement(child)) {
      statements.push(t.expressionStatement(t.callExpression(t.memberExpression(view, t.identifier(adder)), [buildNativeViewExpression(child, nextName)])))
    } else if (t.isJSXExpressionContainer(child) && !t.isJSXEmptyExpression(child.expression)) {
      statements.push(t.expressionStatement(t.callExpression(t.memberExpression(view, t.identifier(adder)), [child.expression])))
    } else if (t.isJSXText(child) && child.value.trim()) {
      throw new Error(`Text children are not supported in Windows native JSX <${tag}>; use a WinLabel text prop.`)
    }
  }
  statements.push(t.returnStatement(view))
  return t.callExpression(t.arrowFunctionExpression([], t.blockStatement(statements)), [])
}

function jsxTagName(name) {
  if (t.isJSXIdentifier(name)) return name.name
  return ''
}

function jsxAttributeValue(value) {
  if (!value) return t.booleanLiteral(true)
  if (t.isStringLiteral(value)) return value
  if (t.isJSXExpressionContainer(value) && !t.isJSXEmptyExpression(value.expression)) return value.expression
  throw new Error('Unsupported Windows native JSX attribute value.')
}

export function geaEmptyIrPlugin() {
  return {
    name: 'gea-empty-ir',
    closeBundle() {
      if (!process.env.GEA_IR_OUT) return
      mkdirSync(dirname(process.env.GEA_IR_OUT), { recursive: true })
      writeFileSync(
        process.env.GEA_IR_OUT,
        `${JSON.stringify(
          {
            schema: 'gea-ir',
            version: 1,
            entry: 'index.ts',
            modules: [],
            components: [],
            stores: [],
            hostCapabilities: [],
          },
          null,
          2,
        )}\n`,
      )
    },
  }
}

// The module-graph recorder lives in @geastack/core; it is resolved from this
// package's location so an app's own vite.config.ts needs one import.
export function geaModuleGraphPluginsFromCore(options = {}) {
  // @geastack/core's exports map does not expose scripts/, so the plugin is
  // located next to the package's own package.json (which is exported).
  let corePackage
  try {
    corePackage = requireBabel.resolve('@geastack/core/package.json')
  } catch {
    if (process.env.GEA_VITE_MODULE_GRAPH_OUT) {
      throw new Error('@geastack/vite-plugin-windows-native: cannot resolve @geastack/core, which records the module graph the native compiler reads')
    }
    return []
  }
  const corePlugin = pathToFileURL(resolve(dirname(corePackage), 'scripts/gea-vite-module-graph-plugin.mjs')).href
  // Dynamic import is asynchronous; Vite accepts a promise of plugins in the
  // `plugins` array, so the resolution happens during config load.
  return import(corePlugin).then((module) => module.geaModuleGraphPlugins({ entryReachableOnly: true, ...options }))
}

export function windowsNativeVitePlugins(options = {}) {
  return [windowsNativeJsxPlugin(), geaEmptyIrPlugin(), geaModuleGraphPluginsFromCore(options.moduleGraph)]
}
