#!/usr/bin/env node
// Generates the UI contract files under contracts/ from the renderer/main
// sources, so a second UI (native/livi-ui) can be built against the same
// surface the React renderer uses. Everything is read statically with the
// TypeScript compiler API: nothing here imports Electron or React.
//
//   node scripts/contracts-gen.mts          write contracts/
//   node scripts/contracts-gen.mts --check  fail when contracts/ is stale
//
// Inputs
//   src/preload/index.ts                    calls (invoke/send) and events (on)
//   src/main/shared/types/Config.ts         config keys, enums, constants
//   src/main/shared/types/DefaultConfig.ts  defaults
//   src/renderer/src/routes/schemas/*.ts    settings tree
//   src/renderer/src/locales/*.json         translations
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
// TypeScript 7 (native) no longer ships the classic compiler API; the generator
// reads sources with TypeScript 5 installed under the alias "typescript5".
import ts from 'typescript5'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const OUT = join(ROOT, 'contracts')
const CHECK = process.argv.includes('--check')

type Json = null | boolean | number | string | Json[] | { [k: string]: Json }

// ---------------------------------------------------------------- helpers

function source(rel: string): ts.SourceFile {
  const file = join(ROOT, rel)
  return ts.createSourceFile(file, readFileSync(file, 'utf8'), ts.ScriptTarget.Latest, true)
}

function text(node: ts.Node, sf: ts.SourceFile): string {
  return node.getText(sf).replace(/\s+/g, ' ').trim()
}

/** Type text with member separators kept, so multi-line object types stay readable. */
function typeText(node: ts.Node, sf: ts.SourceFile): string {
  return node
    .getText(sf)
    .replace(/\r?\n\s*/g, '; ')
    .replace(/([{(<,|&=])\s*;\s*/g, '$1 ')
    .replace(/\s*;\s*([})>|&])/g, ' $1')
    .replace(/;\s*;/g, ';')
    .replace(/\s+/g, ' ')
    .trim()
}

function propName(name: ts.PropertyName, sf: ts.SourceFile): string {
  if (ts.isIdentifier(name) || ts.isStringLiteral(name) || ts.isNumericLiteral(name)) {
    return name.text
  }
  return text(name, sf)
}

function leadingDoc(node: ts.Node, sf: ts.SourceFile): string | undefined {
  const ranges = ts.getLeadingCommentRanges(sf.getFullText(), node.getFullStart()) ?? []
  const lines = ranges.map((r) =>
    sf
      .getFullText()
      .slice(r.pos, r.end)
      .replace(/^\/\*\*?|\*\/$/g, '')
      .split('\n')
      .map((l) => l.replace(/^\s*(\/\/|\*)\s?/, '').trim())
      .filter(Boolean)
      .join(' ')
  )
  const doc = lines.join(' ').trim()
  return doc || undefined
}

function stableJson(value: unknown): string {
  return `${JSON.stringify(value, null, 2)}\n`
}

// ------------------------------------------------- static expression eval

/** Resolves `export const NAME = <expr>` from a module file, following relative
 *  and @shared imports. Depth-limited so a cycle can never hang the generator. */
class ModuleResolver {
  private cache = new Map<string, ts.SourceFile>()

  resolveImport(fromFile: string, spec: string): string | undefined {
    let base: string
    if (spec.startsWith('@shared/')) base = join(ROOT, 'src/main/shared', spec.slice('@shared/'.length))
    else if (spec.startsWith('@renderer/'))
      base = join(ROOT, 'src/renderer/src', spec.slice('@renderer/'.length))
    else if (spec.startsWith('.')) base = resolve(dirname(fromFile), spec)
    else return undefined
    for (const cand of [base, `${base}.ts`, `${base}.tsx`, join(base, 'index.ts'), join(base, 'index.tsx')]) {
      if (existsSync(cand) && !cand.endsWith('/')) {
        try {
          if (readFileSync(cand).length >= 0 && !isDirectory(cand)) return cand
        } catch {
          /* directory or unreadable */
        }
      }
    }
    return undefined
  }

  file(path: string): ts.SourceFile {
    let sf = this.cache.get(path)
    if (!sf) {
      sf = ts.createSourceFile(path, readFileSync(path, 'utf8'), ts.ScriptTarget.Latest, true)
      this.cache.set(path, sf)
    }
    return sf
  }

  /** The initializer of a top-level `const NAME` in `path`, following
   *  `export * from` and `export { NAME } from` re-exports (index files). */
  topLevelConst(
    path: string,
    name: string,
    seen = new Set<string>()
  ): { node: ts.Expression; sf: ts.SourceFile } | undefined {
    if (seen.has(`${path}#${name}`)) return undefined
    seen.add(`${path}#${name}`)
    const sf = this.file(path)
    for (const st of sf.statements) {
      if (ts.isVariableStatement(st)) {
        for (const d of st.declarationList.declarations) {
          if (ts.isIdentifier(d.name) && d.name.text === name && d.initializer) {
            return { node: d.initializer, sf }
          }
        }
      }
    }
    for (const st of sf.statements) {
      if (!ts.isExportDeclaration(st) || !st.moduleSpecifier) continue
      const target = this.resolveImport(path, (st.moduleSpecifier as ts.StringLiteral).text)
      if (!target) continue
      if (!st.exportClause) {
        const found = this.topLevelConst(target, name, seen)
        if (found) return found
      } else if (ts.isNamedExports(st.exportClause)) {
        for (const el of st.exportClause.elements) {
          if (el.name.text === name) {
            return this.topLevelConst(target, (el.propertyName ?? el.name).text, seen)
          }
        }
      }
    }
    return undefined
  }

  /** Where an identifier used in `sf` comes from: a local const or an import. */
  locate(sf: ts.SourceFile, name: string): { node: ts.Expression; sf: ts.SourceFile } | undefined {
    const local = this.topLevelConst(sf.fileName, name)
    if (local) return local
    for (const st of sf.statements) {
      if (!ts.isImportDeclaration(st) || !st.importClause?.namedBindings) continue
      if (!ts.isNamedImports(st.importClause.namedBindings)) continue
      for (const el of st.importClause.namedBindings.elements) {
        if (el.name.text !== name) continue
        const spec = (st.moduleSpecifier as ts.StringLiteral).text
        const target = this.resolveImport(sf.fileName, spec)
        if (!target) return undefined
        const exported = (el.propertyName ?? el.name).text
        return this.topLevelConst(target, exported)
      }
    }
    return undefined
  }
}

function isDirectory(p: string): boolean {
  try {
    return readdirSync(p) !== undefined
  } catch {
    return false
  }
}

/** Loop variables while expanding `[...].map((id, i) => ({...}))`. */
type Env = Record<string, Json>

/** Turns an object-literal-ish expression into JSON. Anything that cannot be
 *  represented statically is kept as a tagged marker so nothing is lost.
 *  `hops` counts identifier resolutions only (the cycle guard); structural
 *  recursion is bounded by the source itself. */
function evalExpr(
  node: ts.Expression,
  sf: ts.SourceFile,
  res: ModuleResolver,
  hops = 0,
  env: Env = {}
): Json {
  if (hops > 16) return { $unresolved: text(node, sf) }
  if (ts.isParenthesizedExpression(node) || ts.isAsExpression(node) || ts.isSatisfiesExpression(node)) {
    return evalExpr(node.expression, sf, res, hops, env)
  }
  if (ts.isTypeAssertionExpression(node)) return evalExpr(node.expression, sf, res, hops, env)
  if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) return node.text
  if (ts.isNumericLiteral(node)) return Number(node.text)
  if (ts.isPrefixUnaryExpression(node) && node.operator === ts.SyntaxKind.MinusToken) {
    const inner = evalExpr(node.operand, sf, res, hops, env)
    return typeof inner === 'number' ? -inner : { $unresolved: text(node, sf) }
  }
  if (node.kind === ts.SyntaxKind.TrueKeyword) return true
  if (node.kind === ts.SyntaxKind.FalseKeyword) return false
  if (node.kind === ts.SyntaxKind.NullKeyword) return null
  if (ts.isArrayLiteralExpression(node)) {
    const out: Json[] = []
    for (const el of node.elements) {
      if (ts.isSpreadElement(el)) {
        const inner = evalExpr(el.expression, sf, res, hops, env)
        if (Array.isArray(inner)) out.push(...inner)
        else out.push({ $spread: text(el.expression, sf) })
      } else out.push(evalExpr(el, sf, res, hops, env))
    }
    return out
  }
  if (ts.isObjectLiteralExpression(node)) {
    const out: { [k: string]: Json } = {}
    for (const p of node.properties) {
      if (ts.isPropertyAssignment(p)) out[propName(p.name, sf)] = evalExpr(p.initializer, sf, res, hops, env)
      else if (ts.isShorthandPropertyAssignment(p)) {
        out[p.name.text] = evalIdentifier(p.name, sf, res, hops, env)
      } else if (ts.isSpreadAssignment(p)) {
        const inner = evalExpr(p.expression, sf, res, hops, env)
        if (inner && typeof inner === 'object' && !Array.isArray(inner)) Object.assign(out, inner)
        else out[`$spread:${text(p.expression, sf)}`] = inner
      } else if (ts.isMethodDeclaration(p)) {
        out[propName(p.name, sf)] = { $fn: propName(p.name, sf) }
      }
    }
    return out
  }
  if (ts.isIdentifier(node)) return evalIdentifier(node, sf, res, hops, env)
  if (ts.isArrowFunction(node) || ts.isFunctionExpression(node)) {
    return { $fn: text(node, sf).slice(0, 160) }
  }
  if (ts.isTemplateExpression(node)) {
    const evaluated = evalWithEnv(text(node, sf), env)
    return evaluated !== undefined ? evaluated : { $template: text(node, sf) }
  }
  if (ts.isCallExpression(node)) {
    const expanded = expandMap(node, sf, res, hops, env)
    if (expanded !== undefined) return expanded
    const evaluated = Object.keys(env).length ? evalWithEnv(text(node, sf), env) : undefined
    return evaluated !== undefined ? evaluated : { $call: text(node, sf).slice(0, 160) }
  }
  if (ts.isPropertyAccessExpression(node)) {
    // Enum members like CarType.Diesel: resolve the enum value if it is in Config.ts.
    const value = enumMember(text(node, sf))
    if (value !== undefined) return value
    const evaluated = Object.keys(env).length ? evalWithEnv(text(node, sf), env) : undefined
    return evaluated !== undefined ? evaluated : { $ref: text(node, sf) }
  }
  if (ts.isBinaryExpression(node) || ts.isConditionalExpression(node)) {
    const evaluated = Object.keys(env).length ? evalWithEnv(text(node, sf), env) : undefined
    // Runtime conditions such as `hidden: window.app?.platform !== 'linux'`.
    return evaluated !== undefined ? evaluated : { $expr: text(node, sf).slice(0, 160) }
  }
  return { $unresolved: text(node, sf).slice(0, 160) }
}

/** Evaluates a small pure expression (template strings, `i + 1`, `id.slice(1)`)
 *  with the loop variables bound. Only reached while expanding a `.map`. */
function evalWithEnv(expr: string, env: Env): Json | undefined {
  if (!Object.keys(env).length) return undefined
  if (/\b(window|process|require|import|globalThis|document)\b/.test(expr)) return undefined
  try {
    const fn = new Function(...Object.keys(env), `return (${expr});`)
    const v = fn(...Object.values(env))
    return typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean' || v === null
      ? v
      : undefined
  } catch {
    return undefined
  }
}

/** `(['a', 'b'] as const).map((id, i) => ({...}))` → the expanded array. */
function expandMap(
  node: ts.CallExpression,
  sf: ts.SourceFile,
  res: ModuleResolver,
  hops: number,
  env: Env
): Json | undefined {
  if (!ts.isPropertyAccessExpression(node.expression) || node.expression.name.text !== 'map') {
    return undefined
  }
  const source = evalExpr(node.expression.expression, sf, res, hops, env)
  const fn = node.arguments[0]
  if (!Array.isArray(source) || !fn || !ts.isArrowFunction(fn)) return undefined
  const [itemParam, indexParam] = fn.parameters.map((p) => text(p.name, sf))
  let body: ts.Expression | undefined
  if (ts.isBlock(fn.body)) {
    const ret = fn.body.statements.find(ts.isReturnStatement)
    body = ret?.expression
  } else body = fn.body
  if (!body) return undefined
  return source.map((item, i) => {
    const inner: Env = { ...env }
    if (itemParam) inner[itemParam] = item
    if (indexParam) inner[indexParam] = i
    return evalExpr(body, sf, res, hops, inner)
  })
}

function evalIdentifier(
  id: ts.Identifier,
  sf: ts.SourceFile,
  res: ModuleResolver,
  hops: number,
  env: Env
): Json {
  if (id.text === 'undefined') return null
  if (id.text in env) return env[id.text]
  const found = res.locate(sf, id.text)
  if (!found) {
    // Components, functions and anything else imported from non-module paths.
    return /^[A-Z]/.test(id.text) ? { $component: id.text } : { $fn: id.text }
  }
  if (ts.isArrowFunction(found.node) || ts.isFunctionExpression(found.node)) {
    // A capitalised function is a React component (settings custom pages).
    if (/^[A-Z]/.test(id.text)) return { $component: id.text }
    const calls = text(found.node, found.sf).match(/window\.app\?\.(\w+)/)
    return calls ? { $fn: id.text, $call: `app.${calls[1]}` } : { $fn: id.text }
  }
  return evalExpr(found.node, found.sf, res, hops + 1, env)
}

// ------------------------------------------------------------ 1. ui-api

type Call = {
  name: string
  params: { name: string; type: string; optional: boolean }[]
  returns: string
  transport: 'invoke' | 'send' | 'local'
  channel?: string
  channelArgs?: string
}

function collectCalls(): { calls: Call[]; events: { channel: string; via: string }[] } {
  const sf = source('src/preload/index.ts')
  const calls: Call[] = []
  const events: { channel: string; via: string }[] = []

  const roots: Record<string, string> = { api: 'projection', appApi: 'app' }
  for (const st of sf.statements) {
    if (!ts.isVariableStatement(st)) continue
    for (const d of st.declarationList.declarations) {
      if (!ts.isIdentifier(d.name) || !roots[d.name.text] || !d.initializer) continue
      if (!ts.isObjectLiteralExpression(d.initializer)) continue
      walkApi(d.initializer, roots[d.name.text])
    }
  }

  function walkApi(obj: ts.ObjectLiteralExpression, prefix: string) {
    for (const p of obj.properties) {
      if (!ts.isPropertyAssignment(p)) continue
      const name = `${prefix}.${propName(p.name, sf)}`
      const init = p.initializer
      if (ts.isObjectLiteralExpression(init)) {
        walkApi(init, name)
        continue
      }
      if (ts.isArrowFunction(init) || ts.isFunctionExpression(init)) {
        const params = init.parameters.map((prm) => ({
          name: text(prm.name, sf),
          type: prm.type ? typeText(prm.type, sf) : 'unknown',
          optional: Boolean(prm.questionToken || prm.initializer)
        }))
        const returns = init.type ? typeText(init.type, sf) : 'unknown'
        let transport: Call['transport'] = 'local'
        let channel: string | undefined
        let channelArgs: string | undefined
        // `const ch = 'settings'` style channel names inside the function body.
        const localStrings = new Map<string, string>()
        const collect = (n: ts.Node) => {
          if (ts.isVariableDeclaration(n) && ts.isIdentifier(n.name) && n.initializer) {
            if (ts.isStringLiteral(n.initializer)) localStrings.set(n.name.text, n.initializer.text)
          }
          ts.forEachChild(n, collect)
        }
        collect(init.body)
        const visit = (n: ts.Node) => {
          if (
            ts.isCallExpression(n) &&
            ts.isPropertyAccessExpression(n.expression) &&
            ts.isIdentifier(n.expression.expression) &&
            n.expression.expression.text === 'ipcRenderer'
          ) {
            const method = n.expression.name.text
            const first = n.arguments[0]
            const ch =
              first && ts.isStringLiteral(first)
                ? first.text
                : first && ts.isIdentifier(first)
                  ? localStrings.get(first.text)
                  : undefined
            if (method === 'invoke' || method === 'send') {
              if (!channel) {
                transport = method
                channel = ch
                channelArgs = n.arguments
                  .slice(1)
                  .map((a) => text(a, sf))
                  .join(', ')
              }
            } else if (method === 'on' && ch) {
              events.push({ channel: ch, via: name })
            }
          }
          ts.forEachChild(n, visit)
        }
        visit(init.body)
        calls.push({ name, params, returns, transport, channel, channelArgs: channelArgs || undefined })
        continue
      }
      // plain values (platform, compositor)
      calls.push({
        name,
        params: [],
        returns: `value: ${text(init, sf)}`,
        transport: 'local'
      })
    }
  }

  // Events wired outside the api objects (queues drained into handlers).
  const visitTop = (n: ts.Node) => {
    if (
      ts.isCallExpression(n) &&
      ts.isPropertyAccessExpression(n.expression) &&
      n.expression.name.text === 'on' &&
      ts.isIdentifier(n.expression.expression) &&
      n.expression.expression.text === 'ipcRenderer' &&
      n.arguments[0] &&
      ts.isStringLiteral(n.arguments[0])
    ) {
      const ch = n.arguments[0].text
      if (!events.some((e) => e.channel === ch)) events.push({ channel: ch, via: 'preload' })
    }
    ts.forEachChild(n, visitTop)
  }
  visitTop(sf)
  events.sort((a, b) => a.channel.localeCompare(b.channel))
  return { calls, events }
}

// ------------------------------------------------------------ 2. config

const enumTable = new Map<string, Record<string, number | string>>()

function enumMember(dotted: string): number | string | undefined {
  const [e, m] = dotted.split('.')
  return enumTable.get(e)?.[m]
}

function collectConfig() {
  const sf = source('src/main/shared/types/Config.ts')
  const enums: Record<string, Record<string, number | string>> = {}
  const constants: Record<string, Json> = {}
  const aliases: Record<string, string> = {}
  let configType: ts.TypeLiteralNode | undefined

  for (const st of sf.statements) {
    if (ts.isEnumDeclaration(st)) {
      let next = 0
      const members: Record<string, number | string> = {}
      for (const m of st.members) {
        const key = propName(m.name, sf)
        if (m.initializer) {
          if (ts.isNumericLiteral(m.initializer)) next = Number(m.initializer.text)
          else if (ts.isStringLiteral(m.initializer)) {
            members[key] = m.initializer.text
            continue
          } else if (
            ts.isPrefixUnaryExpression(m.initializer) &&
            ts.isNumericLiteral(m.initializer.operand)
          ) {
            next = -Number(m.initializer.operand.text)
          }
        }
        members[key] = next
        next += 1
      }
      enums[st.name.text] = members
      enumTable.set(st.name.text, members)
    } else if (ts.isVariableStatement(st)) {
      for (const d of st.declarationList.declarations) {
        if (ts.isIdentifier(d.name) && d.initializer) {
          constants[d.name.text] = evalExpr(d.initializer, sf, new ModuleResolver())
        }
      }
    } else if (ts.isTypeAliasDeclaration(st)) {
      if (st.name.text === 'Config' && ts.isTypeLiteralNode(st.type)) configType = st.type
      else aliases[st.name.text] = text(st.type, sf)
    }
  }
  if (!configType) throw new Error('Config type not found')

  const dsf = source('src/main/shared/types/DefaultConfig.ts')
  const dres = new ModuleResolver()
  const defaults: { [k: string]: Json } = {}
  for (const st of dsf.statements) {
    if (!ts.isVariableStatement(st)) continue
    for (const d of st.declarationList.declarations) {
      if (ts.isIdentifier(d.name) && d.name.text === 'DEFAULT_CONFIG' && d.initializer) {
        const v = evalExpr(d.initializer, dsf, dres)
        if (v && typeof v === 'object' && !Array.isArray(v)) Object.assign(defaults, v)
      }
    }
  }

  const keys = configType.members
    .filter(ts.isPropertySignature)
    .map((m) => {
      const name = propName(m.name, sf)
      const tsType = m.type ? typeText(m.type, sf) : 'unknown'
      return {
        name,
        optional: Boolean(m.questionToken),
        ts: tsType,
        kind: kindOf(tsType, enums, aliases),
        default: name in defaults ? defaults[name] : undefined,
        doc: leadingDoc(m, sf)
      }
    })

  return { keys, enums, constants, aliases }
}

function kindOf(
  tsType: string,
  enums: Record<string, unknown>,
  aliases: Record<string, string>
): Json {
  const t = tsType.trim()
  if (t === 'boolean') return 'boolean'
  if (t === 'number') return 'number'
  if (t === 'string') return 'string'
  if (/^(['"][^'"]*['"]\s*\|\s*)*['"][^'"]*['"]$/.test(t)) {
    return { literal: t.split('|').map((s) => s.trim().replace(/^['"]|['"]$/g, '')) }
  }
  if (enums[t]) return { enum: t }
  if (t.endsWith('[]') || t.startsWith('Array<')) return 'array'
  if (t.startsWith('Record<') || t.startsWith('{') || t.startsWith('Partial<')) return 'object'
  if (aliases[t]) return { alias: t, ts: aliases[t] }
  if (t.includes('|')) return { union: t }
  return { ts: t }
}

// ---------------------------------------------------------- 3. settings

function collectSettings() {
  const res = new ModuleResolver()
  const root = join(ROOT, 'src/renderer/src/routes/schemas/schema.ts')
  const found = res.topLevelConst(root, 'settingsSchema')
  if (!found) throw new Error('settingsSchema not found')
  const tree = evalExpr(found.node, found.sf, res)

  const fields: { path: string; type: string; route: string; labelKey?: string }[] = []
  const counts: Record<string, number> = {}
  const components = new Set<string>()
  const walk = (node: Json, route: string[]) => {
    if (!node || typeof node !== 'object' || Array.isArray(node)) return
    const type = typeof node.type === 'string' ? node.type : ''
    if (type) counts[type] = (counts[type] ?? 0) + 1
    const here = type === 'route' && typeof node.route === 'string' ? [...route, node.route] : route
    if (type && type !== 'route') {
      fields.push({
        path: typeof node.path === 'string' ? node.path : '',
        type,
        route: here.join('/'),
        labelKey: typeof node.labelKey === 'string' ? node.labelKey : undefined
      })
    }
    const comp = node.component
    if (comp && typeof comp === 'object' && !Array.isArray(comp) && typeof comp.$component === 'string') {
      components.add(comp.$component)
    }
    const children = node.children
    if (Array.isArray(children)) for (const c of children) walk(c, here)
  }
  walk(tree, [])
  return { tree, fields, counts, components: [...components].sort() }
}

// ----------------------------------------------------------- 4. locales

function collectLocales() {
  const dir = join(ROOT, 'src/renderer/src/locales')
  const files = readdirSync(dir).filter((f) => f.endsWith('.json')).sort()
  const flat = (o: Json, prefix = ''): string[] =>
    o && typeof o === 'object' && !Array.isArray(o)
      ? Object.entries(o).flatMap(([k, v]) => flat(v, prefix ? `${prefix}.${k}` : k))
      : [prefix]
  const out: Record<string, string> = {}
  const keysByLang: Record<string, string[]> = {}
  for (const f of files) {
    const raw = readFileSync(join(dir, f), 'utf8')
    out[`locales/${f}`] = raw.endsWith('\n') ? raw : `${raw}\n`
    keysByLang[f.replace('.json', '')] = flat(JSON.parse(raw)).sort()
  }
  const en = keysByLang.en ?? []
  const report: Record<string, { missing: string[]; extra: string[] }> = {}
  for (const [lang, keys] of Object.entries(keysByLang)) {
    if (lang === 'en') continue
    report[lang] = {
      missing: en.filter((k) => !keys.includes(k)),
      extra: keys.filter((k) => !en.includes(k))
    }
  }
  return { files: out, keys: en, report }
}

// ----------------------------------------------------------------- main

function main() {
  const { calls, events } = collectCalls()
  const config = collectConfig()
  const settings = collectSettings()
  const locales = collectLocales()

  const header = {
    $generated: 'scripts/contracts-gen.ts — do not edit by hand; run `pnpm contracts:gen`'
  }
  const files: Record<string, string> = {
    'ui-api.json': stableJson({
      ...header,
      summary: {
        calls: calls.length,
        invoke: calls.filter((c) => c.transport === 'invoke').length,
        send: calls.filter((c) => c.transport === 'send').length,
        local: calls.filter((c) => c.transport === 'local').length,
        events: events.length
      },
      calls,
      events
    }),
    'config.schema.json': stableJson({
      ...header,
      summary: { keys: config.keys.length, enums: Object.keys(config.enums).length },
      keys: config.keys,
      enums: config.enums,
      constants: config.constants,
      aliases: config.aliases
    }),
    'settings-schema.json': stableJson({
      ...header,
      summary: { fields: settings.fields.length, byType: settings.counts, customComponents: settings.components },
      fields: settings.fields,
      tree: settings.tree
    }),
    'locale-keys.json': stableJson({ ...header, count: locales.keys.length, keys: locales.keys, report: locales.report }),
    ...locales.files
  }

  let stale = 0
  for (const [rel, content] of Object.entries(files)) {
    const path = join(OUT, rel)
    if (CHECK) {
      const current = existsSync(path) ? readFileSync(path, 'utf8') : ''
      if (current !== content) {
        stale += 1
        console.error(`stale: ${relative(ROOT, path)}`)
      }
    } else {
      mkdirSync(dirname(path), { recursive: true })
      writeFileSync(path, content)
      console.log(`wrote ${relative(ROOT, path)}`)
    }
  }
  if (CHECK) {
    if (stale) {
      console.error(`${stale} contract file(s) out of date — run \`pnpm contracts:gen\``)
      process.exit(1)
    }
    console.log('contracts up to date')
  }
}

main()
