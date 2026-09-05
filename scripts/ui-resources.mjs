// Assembles the runtime resources of native/livi-ui into out/ui:
//   fonts/   Roboto (WOFF, read by FreeType) from @fontsource/roboto
//   locales/ contracts/locales/*.json
// Used by build-native.mjs and by hand for a development copy on a device.
import { copyFileSync, mkdirSync, readdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')

export function assembleUiResources(outDir = join(root, 'out', 'ui')) {
  const fontsSrc = join(root, 'node_modules', '@fontsource', 'roboto', 'files')
  const fontsDst = join(outDir, 'fonts')
  mkdirSync(fontsDst, { recursive: true })
  for (const weight of ['400', '500', '700']) {
    const name = `roboto-latin-${weight}-normal.woff`
    copyFileSync(join(fontsSrc, name), join(fontsDst, name))
  }
  const localesSrc = join(root, 'contracts', 'locales')
  const localesDst = join(outDir, 'locales')
  mkdirSync(localesDst, { recursive: true })
  for (const f of readdirSync(localesSrc).filter((f) => f.endsWith('.json'))) {
    copyFileSync(join(localesSrc, f), join(localesDst, f))
  }
  return outDir
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  console.log(`ui resources → ${assembleUiResources()}`)
}
