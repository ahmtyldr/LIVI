import { type IpcMainEvent, type IpcMainInvokeEvent, ipcMain } from 'electron'

type InvokeHandler<TArgs extends unknown[] = unknown[], TResult = unknown> = (
  event: IpcMainInvokeEvent,
  ...args: TArgs
) => TResult | Promise<TResult>

type EventListener<TArgs extends unknown[] = unknown[]> = (
  event: IpcMainEvent,
  ...args: TArgs
) => void

// Every handler is also kept here so a second UI transport (the JSON-RPC
// bridge in @main/ui-bridge) can dispatch the same channels without going
// through Electron's ipcMain.
const invokeHandlers = new Map<string, InvokeHandler>()
const eventListeners = new Map<string, EventListener>()

export function registerIpcHandle<TArgs extends unknown[] = unknown[], TResult = unknown>(
  channel: string,
  handler: InvokeHandler<TArgs, TResult>
): void {
  invokeHandlers.set(channel, handler as InvokeHandler)
  ipcMain.removeHandler(channel)
  ipcMain.handle(channel, handler as Parameters<typeof ipcMain.handle>[1])
}

export function registerIpcOn<TArgs extends unknown[] = unknown[]>(
  channel: string,
  listener: EventListener<TArgs>
): void {
  eventListeners.set(channel, listener as EventListener)
  ipcMain.removeAllListeners(channel)
  ipcMain.on(channel, listener as Parameters<typeof ipcMain.on>[1])
}

/** The channels a handler or listener has been registered for. */
export function registeredChannels(): { invoke: string[]; send: string[] } {
  return { invoke: [...invokeHandlers.keys()], send: [...eventListeners.keys()] }
}

/** Runs the invoke handler registered for `channel` with a caller-supplied
 *  event object (the bridge passes a synthetic sender). */
export async function invokeChannel(
  channel: string,
  event: IpcMainInvokeEvent,
  ...args: unknown[]
): Promise<unknown> {
  const handler = invokeHandlers.get(channel)
  if (!handler) throw new Error(`no invoke handler for '${channel}'`)
  return await handler(event, ...args)
}

/** Runs the fire-and-forget listener registered for `channel`. */
export function emitChannel(channel: string, event: IpcMainEvent, ...args: unknown[]): boolean {
  const listener = eventListeners.get(channel)
  if (!listener) return false
  listener(event, ...args)
  return true
}
