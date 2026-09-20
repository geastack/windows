// @geastack/geatsc-plugin-windows-native — the Windows SDK as a compiler plugin.
//
// A `CompilerPlugin` for geatsc (loaded through `--plugin` or collected by
// `gea build` from the app's dependencies through `@geastack/windows/geatsc-plugin`).
// It states, as data, everything the compiler must know to lower a program
// written against `@geastack/windows/*`:
//
//   - which declared type names are native handles and what C++ carries them
//   - what every member, constructor and free function renders to
//   - the header each carrier is declared by, and the constants' values
//
// and it writes the native bridge (header + handle table) beside the emitted
// unit when a build says it is a Windows-native build. Nothing here is a
// message send: the Win32 target defines every thunk by hand, so a spelling
// is always `thunk({receiver}, {arg0}, ...)`.
//
// The one thing that is not data is the JSX rewrite: `<WinStackView spacing={8}>`
// has to become construction before the checker runs, for the reasons the
// compiler's own Apple plugin documents in its `jsx.ts`.

import fs from 'node:fs'
import path from 'node:path'
import ts from 'typescript'
import { inertPluginInstance } from '@geastack/compiler/plugin'
import type { CompilerPlugin, PluginInstance, PluginOptions, PluginSourceFile } from '@geastack/compiler/plugin'

import {
  cppType,
  generateWindowsBridgeMetadata,
  generateWindowsNativeBridgeHeader,
  generateWindowsNativeBridgeSource,
  windowsSdkFixture,
  type WindowsBridgeClassMetadata,
  type WindowsBridgeMetadata,
  type WindowsParameterDefinition,
  type WindowsTypeReference,
} from '@geastack/windows'

// The compiler's own `HostMember`/`HostConstructor` are not re-exported from
// its plugin entry; these are the two arms of each this plugin renders,
// structurally identical to the compiler's so the maps below type-check
// against `PluginCapabilities`.
export type HostMember =
  | { readonly kind: 'property'; readonly emit: string | null; readonly store: string | null }
  | { readonly kind: 'method'; readonly emit: string; readonly arity: number }
export interface HostConstructor {
  readonly emit: string
  readonly arity: number
}

export const windowsNativeBridgeHeader = 'gea/windows/native_bridge.h'
export const windowsNativeBridgeSource = 'gea/windows/native_bridge.cpp'

/** The option (or environment variable) naming the bridge metadata a build wrote. */
export const windowsMetadataOption = 'windows.metadata'
export const windowsMetadataEnvironment = 'GEA_WINDOWS_NATIVE_METADATA'

// --- host tables ---------------------------------------------------------------

export interface WindowsHostTables {
  nativeTypes: Map<string, string>
  nativeBases: Map<string, string>
  hostMembers: Map<string, HostMember>
  hostConstructors: Map<string, HostConstructor>
  hostFunctions: Map<string, string>
  nativeConstants: Map<string, string>
  nativeIncludes: Map<string, string>
  hostPreambles: Map<string, readonly string[]>
}

const argumentTemplates = (parameters: readonly WindowsParameterDefinition[], library: string, offset = 0): string[] =>
  parameters.map((parameter, index) => argumentTemplate(parameter.type, library, `{arg${index + offset}}`))

// A callback parameter is declared `std::function<...>` on the thunk; the
// compiler hands a closure value, which converts explicitly. Everything else
// converts implicitly (a wrapper struct, a double, a std::string).
const argumentTemplate = (type: WindowsTypeReference, library: string, slot: string): string =>
  type.kind === 'function' ? `${cppType(type, library)}(${slot})` : slot

const constantText = (type: WindowsTypeReference, value: string | number | boolean): string => {
  if (type.kind === 'primitive' && type.name === 'string') return `std::string(${JSON.stringify(String(value))})`
  if (type.kind === 'primitive' && type.name === 'boolean') return value ? 'true' : 'false'
  return String(value)
}

const classChain = (metadata: WindowsBridgeMetadata, key: string): WindowsBridgeClassMetadata[] => {
  const chain: WindowsBridgeClassMetadata[] = []
  const seen = new Set<string>()
  let current: string | undefined = key
  while (current && !seen.has(current)) {
    seen.add(current)
    const entry: WindowsBridgeClassMetadata | undefined = metadata.classes[current]
    if (!entry) break
    chain.push(entry)
    current = entry.extends
  }
  return chain
}

export function createWindowsHostTables(metadata: WindowsBridgeMetadata): WindowsHostTables {
  const tables: WindowsHostTables = {
    nativeTypes: new Map(),
    nativeBases: new Map(),
    hostMembers: new Map(),
    hostConstructors: new Map(),
    hostFunctions: new Map(),
    nativeConstants: new Map(),
    nativeIncludes: new Map(),
    hostPreambles: new Map(),
  }
  for (const entry of Object.values(metadata.structs)) {
    tables.nativeTypes.set(entry.name, entry.wrapper)
    tables.nativeTypes.set(`${entry.library}.${entry.name}`, entry.wrapper)
    tables.nativeIncludes.set(entry.wrapper, windowsNativeBridgeHeader)
    for (const field of entry.fields) {
      tables.hostMembers.set(`${entry.wrapper}.${field.name}`, {
        kind: 'property',
        emit: `{receiver}.${field.name}`,
        store: `{receiver}.${field.name} = {value}`,
      })
    }
  }
  for (const [key, entry] of Object.entries(metadata.classes)) {
    tables.nativeTypes.set(entry.name, entry.wrapper)
    tables.nativeTypes.set(key, entry.wrapper)
    tables.nativeIncludes.set(entry.wrapper, windowsNativeBridgeHeader)
    if (entry.extends) {
      const base = metadata.classes[entry.extends]
      if (base) tables.nativeBases.set(entry.wrapper, base.wrapper)
    }
    // `constructor` is inherited from Object.prototype on any plain object, so a
    // class that states none answers with a Function here; own-property or nothing.
    const construct = Object.prototype.hasOwnProperty.call(entry, 'constructor') ? entry.constructor : undefined
    if (construct) {
      const args = argumentTemplates(construct.parameters, entry.library).join(', ')
      tables.hostConstructors.set(entry.wrapper, {
        emit: `${entry.wrapper}(static_cast<double>(${construct.thunk}(${args})))`,
        arity: construct.parameters.length,
      })
    }
    // Members are filed under every carrier that can reach them, the class's
    // own and each descendant's: the compiler keys a member by the exact
    // carrier the checker bound, and a `WinLabel` reaching `addSubview`
    // through `WinView`'s declaration is still a `WinLabel` at the site.
    for (const declaring of classChain(metadata, key)) {
      for (const method of Object.values(declaring.methods)) {
        const memberKey = `${entry.wrapper}.${method.name}`
        if (tables.hostMembers.has(memberKey)) continue
        const args = method.static
          ? argumentTemplates(method.parameters, declaring.library)
          : ['{receiver}', ...argumentTemplates(method.parameters, declaring.library)]
        tables.hostMembers.set(memberKey, { kind: 'method', emit: `${method.thunk}(${args.join(', ')})`, arity: method.parameters.length })
      }
      for (const property of Object.values(declaring.properties)) {
        const memberKey = `${entry.wrapper}.${property.name}`
        if (tables.hostMembers.has(memberKey)) continue
        tables.hostMembers.set(memberKey, {
          kind: 'property',
          emit: `${property.getter}({receiver})`,
          store: property.setter ? `${property.setter}({receiver}, ${argumentTemplate(property.type, declaring.library, '{value}')})` : null,
        })
      }
    }
  }
  for (const entry of Object.values(metadata.functions)) {
    tables.hostFunctions.set(entry.name, entry.thunk)
    tables.hostPreambles.set(entry.thunk, [`#include "${windowsNativeBridgeHeader}"`])
  }
  for (const entry of Object.values(metadata.constants)) {
    tables.nativeConstants.set(entry.name, constantText(entry.type, entry.value))
  }
  return tables
}

// --- JSX over Controls classes -----------------------------------------------------

/**
 * `<WinStackView spacing={8}>{child}</WinStackView>` as the construction it means.
 *
 * Same shape as the compiler's Apple transform, for the same reason: the
 * checker types every JSX element as `JSX.Element`, so a producer reading its
 * answer would learn nothing about the tag's class. Rewritten to an
 * immediately-invoked arrow before the checker runs, the element is ordinary
 * TypeScript and `stack.spacing = 8` is checked against the declared property.
 * Only the element ranges are spliced; every other byte of the file survives.
 */
export function windowsJsxTransform(carriers: ReadonlyMap<string, string>, members: ReadonlyMap<string, unknown>, input: PluginSourceFile): string | null {
  const { fileName, text } = input
  if (!fileName.endsWith('.tsx') && !fileName.endsWith('.jsx')) return null
  if (!text.includes('<') || carriers.size === 0) return null
  const file = ts.createSourceFile(fileName, text, ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX)

  interface Claimed {
    node: ts.JsxSelfClosingElement | ts.JsxElement
    opening: ts.JsxOpeningElement | ts.JsxSelfClosingElement
    tag: string
    children: readonly ts.JsxChild[]
  }
  const claim = (node: ts.Node): Claimed | null => {
    if (ts.isJsxSelfClosingElement(node)) {
      const tag = ts.isIdentifier(node.tagName) ? node.tagName.text : null
      return tag !== null && carriers.has(tag) ? { node, opening: node, tag, children: [] } : null
    }
    if (!ts.isJsxElement(node)) return null
    const tag = ts.isIdentifier(node.openingElement.tagName) ? node.openingElement.tagName.text : null
    return tag !== null && carriers.has(tag) ? { node, opening: node.openingElement, tag, children: node.children } : null
  }
  const roots: Claimed[] = []
  const visit = (node: ts.Node): void => {
    const claimed = claim(node)
    if (claimed) {
      roots.push(claimed)
      return
    }
    ts.forEachChild(node, visit)
  }
  ts.forEachChild(file, visit)
  if (roots.length === 0) return null

  let counter = 0
  const nextName = (): string => `__geaWinView${counter++}`
  const sourceText = (node: ts.Node): string => node.getText(file)
  const render = (claimed: Claimed): string => {
    const view = nextName()
    const carrier = carriers.get(claimed.tag) ?? ''
    const statements = [`const ${view} = new ${claimed.tag}();`]
    for (const attribute of claimed.opening.attributes.properties) {
      if (!ts.isJsxAttribute(attribute) || !ts.isIdentifier(attribute.name)) {
        throw new Error(`<${claimed.tag}> spreads its attributes; a Windows native element states each property it sets, by name`)
      }
      const initializer = attribute.initializer
      let value = 'true'
      if (initializer && ts.isStringLiteral(initializer)) value = sourceText(initializer)
      else if (initializer && ts.isJsxExpression(initializer) && initializer.expression) value = sourceText(initializer.expression)
      else if (initializer) throw new Error(`<${claimed.tag}> has an attribute with no value expression`)
      statements.push(`${view}.${attribute.name.text} = ${value};`)
    }
    const adder = members.has(`${carrier}.addArrangedSubview`) ? 'addArrangedSubview' : 'addSubview'
    for (const child of claimed.children) {
      const nested = claim(child)
      if (nested) {
        statements.push(`${view}.${adder}(${render(nested)});`)
        continue
      }
      if (ts.isJsxExpression(child)) {
        if (child.expression) statements.push(`${view}.${adder}(${sourceText(child.expression)});`)
        continue
      }
      if (ts.isJsxText(child)) {
        if (child.text.trim().length > 0) {
          throw new Error(`<${claimed.tag}> has text between its tags; a Windows control holds subviews, and text belongs to a label's text property`)
        }
        continue
      }
      throw new Error(`<${claimed.tag}> has a child this transform does not read`)
    }
    statements.push(`return ${view};`)
    return `(() => { ${statements.join(' ')} })()`
  }
  let rewritten = text
  for (const claimed of [...roots].reverse()) {
    const start = claimed.node.getStart(file)
    rewritten = rewritten.slice(0, start) + render(claimed) + rewritten.slice(claimed.node.end)
  }
  return rewritten
}

// --- the plugin ------------------------------------------------------------------

function loadMetadata(options: PluginOptions): { metadata: WindowsBridgeMetadata; active: boolean } {
  const stated = options.get(windowsMetadataOption) ?? process.env[windowsMetadataEnvironment] ?? ''
  if (stated.length > 0) {
    return { metadata: JSON.parse(fs.readFileSync(path.resolve(stated), 'utf8')) as WindowsBridgeMetadata, active: true }
  }
  return { metadata: generateWindowsBridgeMetadata(windowsSdkFixture), active: false }
}

export function writeWindowsBridgeArtifacts(metadata: WindowsBridgeMetadata, outDir: string): void {
  const write = (file: string, contents: string): void => {
    if (fs.existsSync(file) && fs.readFileSync(file, 'utf8') === contents) return
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, contents)
  }
  write(path.join(outDir, windowsNativeBridgeHeader), generateWindowsNativeBridgeHeader(metadata))
  write(path.join(outDir, windowsNativeBridgeSource), generateWindowsNativeBridgeSource())
}

export const windowsNativePlugin: CompilerPlugin = {
  name: 'windows-native',
  instantiate: (options: PluginOptions): PluginInstance => {
    const { metadata, active } = loadMetadata(options)
    const tables = createWindowsHostTables(metadata)
    return {
      ...inertPluginInstance,
      transformSource: (input) => windowsJsxTransform(tables.nativeTypes, tables.hostMembers, input),
      // The bridge is written only for a build that named its metadata: this
      // plugin is installed for every app that depends on @geastack/windows,
      // including one built for the web, and a Win32 header in a web build's
      // output would be noise at best.
      ...(active ? { writeArtifacts: (outDir: string) => writeWindowsBridgeArtifacts(metadata, outDir) } : {}),
      capabilities: {
        ...inertPluginInstance.capabilities,
        nativeTypes: tables.nativeTypes,
        nativeBases: tables.nativeBases,
        nativeIncludes: tables.nativeIncludes,
        hostMembers: tables.hostMembers,
        hostConstructors: tables.hostConstructors,
        hostFunctions: tables.hostFunctions,
        hostPreambles: tables.hostPreambles,
        nativeConstants: tables.nativeConstants,
        declarationModules: new Set(['@geastack/windows', '@geastack/windows/*']),
        generatedSupportIncludes: active ? [windowsNativeBridgeHeader] : [],
      },
    }
  },
}

export default windowsNativePlugin
