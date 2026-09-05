// UiHost: the seam between the main process and whatever draws the UI.
// Electron draws it today; native/livi-ui talks over the JSON-RPC bridge.
// Callers ask the host to show a dialog, open a link, broadcast an event or
// quit, and never import BrowserWindow, dialog, shell or app themselves.

import { bridgeEmit } from '@main/ui-bridge/server'
import { app, BrowserWindow, dialog, shell } from 'electron'

export type MessageBoxOptions = {
  type?: 'none' | 'info' | 'error' | 'question' | 'warning'
  title?: string
  message: string
  detail?: string
  buttons?: string[]
  defaultId?: number
  cancelId?: number
  noLink?: boolean
  checkboxLabel?: string
  checkboxChecked?: boolean
}

export type MessageBoxResult = { response: number; checkboxChecked?: boolean }

export interface UiHost {
  readonly kind: 'electron' | 'socket'
  /** Modal question or notice. `parent` is the Electron window when there is one. */
  showMessageBox(options: MessageBoxOptions, parent?: unknown): Promise<MessageBoxResult>
  openExternal(url: string): Promise<void>
  /** Sends a renderer event to every UI: Electron windows and bridge clients. */
  broadcast(channel: string, ...args: unknown[]): void
  quit(): void
  relaunch(): void
}

/** Every live BrowserWindow, or none when Electron is not around. */
function allWindows(): BrowserWindow[] {
  try {
    return BrowserWindow.getAllWindows() ?? []
  } catch {
    return []
  }
}

export const electronUiHost: UiHost = {
  kind: 'electron',
  showMessageBox(options, parent) {
    const win = parent as BrowserWindow | undefined
    return win ? dialog.showMessageBox(win, options) : dialog.showMessageBox(options)
  },
  openExternal(url) {
    return shell.openExternal(url)
  },
  broadcast(channel, ...args) {
    for (const win of allWindows()) {
      try {
        if (typeof win.isDestroyed === 'function' && win.isDestroyed()) continue
        win.webContents.send(channel, ...args)
      } catch (e) {
        console.warn(`[ui-host] send '${channel}' failed (ignored)`, e)
      }
    }
    // The bridge tap mirrors webContents.send today; once every send goes
    // through this host the tap goes and this line is the only fan-out.
  },
  quit() {
    app.quit()
  },
  relaunch() {
    app.relaunch()
  }
}

/** Headless host for LIVI_UI=lvgl. Dialogs become bridge events the native
 *  UI renders itself; until it answers, the default button is taken. */
export const socketUiHost: UiHost = {
  kind: 'socket',
  async showMessageBox(options) {
    bridgeEmit('ui:dialog', options)
    console.log(`[ui-host] dialog "${options.title ?? options.message}" → default button`)
    return { response: options.defaultId ?? 0, checkboxChecked: options.checkboxChecked }
  },
  async openExternal(url) {
    bridgeEmit('ui:open-external', url)
  },
  broadcast(channel, ...args) {
    bridgeEmit(channel, ...args)
  },
  quit() {
    process.exit(0)
  },
  relaunch() {
    // The service manager restarts the process; 75 = EX_TEMPFAIL.
    process.exit(75)
  }
}

let override: UiHost | undefined

export function uiMode(): 'electron' | 'socket' {
  return process.env.LIVI_UI === 'lvgl' || process.env.LIVI_UI === 'socket' ? 'socket' : 'electron'
}

export function getUiHost(): UiHost {
  return override ?? (uiMode() === 'socket' ? socketUiHost : electronUiHost)
}

/** Test seam. */
export function setUiHostForTests(host: UiHost | undefined): void {
  override = host
}

/** Drop-in for `dialog.showMessageBox(window, options)` call sites. */
export function showMessageBox(
  parent: unknown,
  options: MessageBoxOptions
): Promise<MessageBoxResult> {
  return getUiHost().showMessageBox(options, parent)
}
