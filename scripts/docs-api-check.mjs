#!/usr/bin/env node
// docs-api-check: keep the onboarding docs honest against the real SDK API,
// without an Unreal build. A full UBT compile is not practical here, so this is
// a symbol-level gate targeting the drift class behind the 2026-07-20
// phantom-API sweep:
//
//   1. TYPE EXISTENCE - every Playloop type (U/F/A/E-prefixed) the docs name
//      must exist in Source/. Catches the wrong `UPlayloopGameInstanceSubsystem`
//      (real is `UPlayloopSubsystem`) the docs were just fixed for.
//   2. METHOD EXISTENCE - every method the docs call via `->` or a Playloop
//      `::` must be defined in Source/ headers. Catches invented methods.
//
// Run: node scripts/docs-api-check.mjs

import { readFileSync, existsSync, readdirSync, statSync } from 'node:fs'
import { join, resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const CONFIG = {
  fence: 'cpp',
  docs: ['README.md', 'AGENTS.md', 'SETUP_UNREAL.md'],
  srcDir: 'Source',
  typeRe: /\b[UFAE]Playloop\w*/g,
  // Unreal builtins / delegate + container statics that are not SDK methods.
  builtins: [
    'CreateLambda', 'CreateStatic', 'CreateUObject', 'CreateSP', 'CreateRaw', 'CreateWeakLambda',
    'Printf', 'Format', 'Broadcast', 'Bind', 'BindUObject', 'BindLambda', 'Execute', 'ExecuteIfBound',
    'IsBound', 'Get', 'GetWorld', 'GetName', 'IsValid', 'AddToRoot', 'RemoveFromRoot', 'GetSubsystem',
    'GetGameInstance', 'NewObject', 'LoadObject', 'ToString', 'Len', 'Num', 'Add', 'Contains', 'Find',
    'GetDefault', 'GetMutableDefault', 'CreateDefault',
  ],
}

const walk = (dir, out = []) => {
  if (!existsSync(dir)) return out
  for (const e of readdirSync(dir)) {
    const p = join(dir, e)
    if (statSync(p).isDirectory()) walk(p, out)
    else if (/\.(h|cpp)$/.test(p)) out.push(p)
  }
  return out
}

// Over-collect: every Playloop type token and every method-name token that
// appears anywhere in Source. A doc symbol absent from this set is genuinely
// invented; a name reused elsewhere is (safely) treated as real.
const types = new Set()
const methods = new Set()
for (const f of walk(resolve(ROOT, CONFIG.srcDir))) {
  const src = readFileSync(f, 'utf8')
  for (const m of src.matchAll(CONFIG.typeRe)) types.add(m[0])
  for (const m of src.matchAll(/\b([A-Za-z_]\w*)\s*\(/g)) methods.add(m[1])
}

const blockRe = new RegExp('```' + CONFIG.fence + '\\n([\\s\\S]*?)```', 'g')
const problems = []
let blocks = 0

for (const rel of CONFIG.docs) {
  const abs = resolve(ROOT, rel)
  if (!existsSync(abs)) continue
  const text = readFileSync(abs, 'utf8')
  for (const bm of text.matchAll(blockRe)) {
    blocks++
    const code = bm[1]
    // 1. Playloop type existence
    for (const m of code.matchAll(CONFIG.typeRe))
      if (!types.has(m[0]))
        problems.push(`${rel}: type \`${m[0]}\` is referenced but not defined in Source/ (real Playloop type?)`)
    // 2. method existence: `->Method(` and `UPlayloop...::Method(`
    for (const m of code.matchAll(/->\s*([A-Za-z_]\w*)\s*\(/g)) {
      const leaf = m[1]
      if (!methods.has(leaf) && !CONFIG.builtins.includes(leaf))
        problems.push(`${rel}: docs call \`->${leaf}(\` but method \`${leaf}\` is not defined in Source/`)
    }
    for (const m of code.matchAll(/[UFAE]Playloop\w*::\s*([A-Za-z_]\w*)\s*\(/g)) {
      const leaf = m[1]
      if (!methods.has(leaf) && !CONFIG.builtins.includes(leaf))
        problems.push(`${rel}: docs call a Playloop \`::${leaf}(\` but \`${leaf}\` is not defined in Source/`)
    }
  }
}

console.log(`[docs-api-check] ${blocks} ${CONFIG.fence} block(s) checked (${types.size} Playloop types, ${methods.size} method names)`)
if (problems.length) {
  console.error('\n[docs-api-check] FAILED - docs reference an API that does not match Source/:\n')
  for (const p of [...new Set(problems)]) console.error(`  ${p}`)
  process.exit(1)
}
console.log('[docs-api-check] clean: Playloop types and SDK method calls all resolve.')
