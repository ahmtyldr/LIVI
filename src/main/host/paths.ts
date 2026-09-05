// Where the app lives and keeps its data, without the callers touching
// Electron. Under Electron the answers come from `app`; under plain Node
// (LIVI_UI=lvgl, no Electron) `electron` resolves to the npm shim and `app`
// is undefined, so the environment and conventional paths take over.
import { homedir } from 'node:os'
import { join } from 'node:path'
import { app } from 'electron'

type ElectronApp = {
  getPath?(name: 'userData'): string
  getAppPath?(): string
  getVersion?(): string
  isPackaged?: boolean
}

// Each accessor checks the one member it needs: under plain Node `app` is
// undefined, and test doubles often stub a single method.
function electronApp(): ElectronApp | undefined {
  const a = app as unknown as ElectronApp | undefined
  return a && typeof a === 'object' ? a : undefined
}

/** ~/.config/LIVI on Linux: config.json, devices.json, logs, staged driver. */
export function userDataDir(): string {
  const a = electronApp()
  if (typeof a?.getPath === 'function') return a.getPath('userData')
  return process.env.LIVI_USER_DATA ?? join(homedir(), '.config', 'LIVI')
}

/** The application root (app.asar or the source checkout). */
export function appRoot(): string {
  const a = electronApp()
  if (typeof a?.getAppPath === 'function') return a.getAppPath()
  return process.env.LIVI_APP_ROOT ?? process.cwd()
}

export function isPackaged(): boolean {
  const a = electronApp()
  if (a && 'isPackaged' in a) return Boolean(a.isPackaged)
  return process.env.LIVI_PACKAGED === '1'
}

export function appVersion(): string {
  const a = electronApp()
  if (typeof a?.getVersion === 'function') return a.getVersion()
  return process.env.LIVI_VERSION ?? '0.0.0'
}

/** Bundled assets: `resources/` in a package, `assets/` in a checkout. */
export function assetsDir(): string {
  if (isPackaged())
    return process.env.LIVI_RESOURCES ?? process.resourcesPath ?? join(appRoot(), 'assets')
  return join(appRoot(), 'assets')
}
