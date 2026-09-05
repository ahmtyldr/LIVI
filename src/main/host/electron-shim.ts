// Stand-in for the `electron` module in the headless bundle (LIVI_UI=lvgl,
// plain Node). vite.headless.config.mts aliases `electron` here, so modules
// that still import Electron load without it. Everything that must *work*
// without Electron goes through @main/host; what is left here only has to
// not throw when it is touched, or throw a clear message when it is used.
/* eslint-disable @typescript-eslint/no-explicit-any */

const unavailable = (what: string) => () => {
  throw new Error(`${what} is not available without Electron (LIVI_UI=${process.env.LIVI_UI})`)
}

const noop = () => undefined

export const app: any = undefined
export const BrowserWindow: any = undefined
export const dialog: any = undefined
export const shell: any = undefined
export const screen: any = undefined
export const session: any = undefined
export const webContents: any = undefined
export const contextBridge: any = undefined
export const ipcRenderer: any = undefined

/** Handlers are kept by @main/ipc/register; ipcMain only needs to accept them. */
export const ipcMain: any = {
  handle: noop,
  on: noop,
  once: noop,
  removeHandler: noop,
  removeAllListeners: noop
}

export const net: any = {
  request: unavailable('net.request'),
  fetch: (input: any, init?: any) => globalThis.fetch(input, init)
}

export const protocol: any = {
  handle: noop,
  registerSchemesAsPrivileged: noop
}

export default {
  app,
  BrowserWindow,
  dialog,
  shell,
  screen,
  session,
  webContents,
  ipcMain,
  net,
  protocol
}
