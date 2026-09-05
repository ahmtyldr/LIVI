// Mirrors every `webContents.send(channel, ...args)` to the bridge clients.
// The main process reaches renderers through several paths (broadcast
// helpers, BrowserWindow.getAllWindows(), stored WebContents), so rather than
// editing each call site the send method is wrapped once on the shared
// prototype. Session 3 of the LVGL plan replaces this with a UiHost seam.
import type { WebContents } from 'electron'
import { bridgeEmit } from './server'

const TAPPED = Symbol.for('livi.uiBridge.sendTap')

type SendFn = (channel: string, ...args: unknown[]) => void

export function installRendererSendTap(sample: WebContents | undefined): boolean {
  if (!sample) return false
  const proto = Object.getPrototypeOf(sample) as { send?: SendFn; [TAPPED]?: boolean } | null
  if (!proto || typeof proto.send !== 'function' || proto[TAPPED]) return Boolean(proto?.[TAPPED])
  const original = proto.send
  proto.send = function tappedSend(this: WebContents, channel: string, ...args: unknown[]) {
    try {
      bridgeEmit(channel, ...args)
    } catch {
      /* the bridge must never break the renderer path */
    }
    return original.call(this, channel, ...args)
  }
  proto[TAPPED] = true
  return true
}
