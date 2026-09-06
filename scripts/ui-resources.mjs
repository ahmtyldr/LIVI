// Assembles the runtime resources of native/livi-ui into out/ui:
//   fonts/   Roboto (WOFF, read by FreeType) from @fontsource/roboto
//   locales/ contracts/locales/*.json
//   icons/   native/livi-ui/assets/icons (MUI nav icons rasterised to PNG)
//   settings-schema.json  the settings tree (schema-driven Settings pages)
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
    for (const subset of ['latin', 'latin-ext']) {
      const name = `roboto-${subset}-${weight}-normal.woff`
      copyFileSync(join(fontsSrc, name), join(fontsDst, name))
    }
  }
  const localesSrc = join(root, 'contracts', 'locales')
  const localesDst = join(outDir, 'locales')
  mkdirSync(localesDst, { recursive: true })
  for (const f of readdirSync(localesSrc).filter((f) => f.endsWith('.json'))) {
    copyFileSync(join(localesSrc, f), join(localesDst, f))
  }
  copyFileSync(
    join(root, 'contracts', 'settings-schema.json'),
    join(outDir, 'settings-schema.json')
  )
  const iconsSrc = join(root, 'native', 'livi-ui', 'assets', 'icons')
  const iconsDst = join(outDir, 'icons')
  mkdirSync(iconsDst, { recursive: true })
  for (const f of readdirSync(iconsSrc).filter((f) => f.endsWith('.png'))) {
    copyFileSync(join(iconsSrc, f), join(iconsDst, f))
  }
  return outDir
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  console.log(`ui resources → ${assembleUiResources()}`)
}
