// What the main process needs from "a UI it can send events to". Electron's
// WebContents satisfies this structurally; the bridge renderer stands in for
// every connected livi-ui client at once.
import { bridgeClientAlive, bridgeEmit } from '@main/ui-bridge/server'

export type RendererTarget = {
  readonly id: number
  send(channel: string, ...args: unknown[]): void
  isDestroyed(): boolean
}

/** Id the bridge renderer reports; bridge clients themselves use -clientId. */
export const BRIDGE_RENDERER_ID = -1

const bridge: RendererTarget = {
  id: BRIDGE_RENDERER_ID,
  send: (channel, ...args) => bridgeEmit(channel, ...args),
  isDestroyed: () => false
}

export function bridgeRenderer(): RendererTarget {
  return bridge
}

/** True for the bridge renderer itself and for any connected bridge client. */
export function isBridgeRendererAlive(id: number): boolean {
  if (id === BRIDGE_RENDERER_ID) return true
  return id < 0 && bridgeClientAlive(-id)
}
