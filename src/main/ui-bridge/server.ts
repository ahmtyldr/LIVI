// UiBridge: a Unix-socket JSON-RPC 2.0 server that exposes the renderer's IPC
// surface to a second UI process (native/livi-ui). It runs next to Electron,
// so both UIs can talk to the same main process at once; every event the main
// process sends to a renderer is also broadcast to every connected client.
import { existsSync, unlinkSync } from 'node:fs'
import { createServer, type Server, type Socket } from 'node:net'
import { join } from 'node:path'
import { emitChannel, invokeChannel } from '@main/ipc/register'
import type { IpcMainEvent, IpcMainInvokeEvent } from 'electron'
import { type BridgeMethod, buildMethods, describe, LOCAL_VALUES } from './methods'
import {
  decodeFrames,
  encodeFrame,
  isRpcRequest,
  RPC_ERROR,
  type RpcEvent,
  type RpcId,
  type RpcRequest,
  type RpcResponse
} from './protocol'

export type UiBridgeOptions = {
  /** Socket path; default `$LIVI_UI_SOCKET`, then `$XDG_RUNTIME_DIR/livi-ui.sock`, then /tmp. */
  socketPath?: string
  log?: (line: string) => void
}

type Client = { id: number; socket: Socket; buffer: string }

let server: Server | undefined
let socketPath: string | undefined
let nextClientId = 1
const clients = new Set<Client>()
let methods: Map<string, BridgeMethod> | undefined
let log: (line: string) => void = (line) => console.log(line)

export function defaultSocketPath(): string {
  if (process.env.LIVI_UI_SOCKET) return process.env.LIVI_UI_SOCKET
  const dir = process.env.XDG_RUNTIME_DIR || '/tmp'
  return join(
    dir,
    process.env.XDG_RUNTIME_DIR ? 'livi-ui.sock' : `livi-ui-${process.getuid?.() ?? 0}.sock`
  )
}

export function uiBridgeSocketPath(): string | undefined {
  return socketPath
}

/** A stand-in for the Electron event object handlers receive. Handlers that
 *  key on `sender.id` see a negative id no BrowserWindow ever has. */
function syntheticEvent(client: Client): IpcMainInvokeEvent & IpcMainEvent {
  const sender = {
    id: -client.id,
    send: (channel: string, ...args: unknown[]) => sendEvent(client, channel, args),
    once: () => sender,
    on: () => sender,
    isDestroyed: () => client.socket.destroyed
  }
  return {
    sender,
    senderFrame: null,
    frameId: 0,
    processId: 0,
    returnValue: undefined
  } as unknown as IpcMainInvokeEvent & IpcMainEvent
}

function write(client: Client, message: unknown): void {
  if (client.socket.destroyed) return
  try {
    client.socket.write(encodeFrame(message))
  } catch (e) {
    log(`[ui-bridge] write to client ${client.id} failed: ${(e as Error).message}`)
  }
}

function sendEvent(client: Client, channel: string, args: unknown[]): void {
  const event: RpcEvent = { jsonrpc: '2.0', method: 'event', params: { channel, args } }
  write(client, event)
}

/** Forwards a renderer-bound event to every connected bridge client. Wired
 *  into the renderer send path by installRendererSendTap(). */
export function bridgeEmit(channel: string, ...args: unknown[]): void {
  if (!clients.size) return
  const frame = encodeFrame({ jsonrpc: '2.0', method: 'event', params: { channel, args } })
  for (const c of clients) {
    if (!c.socket.destroyed) c.socket.write(frame)
  }
}

export function bridgeClientCount(): number {
  return clients.size
}

/** Whether the client with this bridge id (the positive counterpart of a
 *  synthetic sender id) is still connected. */
export function bridgeClientAlive(clientId: number): boolean {
  for (const c of clients) if (c.id === clientId && !c.socket.destroyed) return true
  return false
}

function error(id: RpcId, code: number, message: string, data?: unknown): RpcResponse {
  return {
    jsonrpc: '2.0',
    id,
    error: data === undefined ? { code, message } : { code, message, data }
  }
}

async function dispatch(client: Client, req: RpcRequest): Promise<RpcResponse | undefined> {
  const id = req.id ?? null
  const params = Array.isArray(req.params) ? req.params : req.params ? [req.params] : []

  if (req.method === 'rpc.describe') return { jsonrpc: '2.0', id, result: describe() }
  if (req.method === 'rpc.ping') return { jsonrpc: '2.0', id, result: 'pong' }

  const local = LOCAL_VALUES[req.method]
  if (local) return { jsonrpc: '2.0', id, result: local() }

  const method = methods?.get(req.method)
  if (!method) return error(id, RPC_ERROR.methodNotFound, `unknown method '${req.method}'`)
  if (method.transport === 'local' || !method.channel) {
    // Subscriptions (onEvent, onTelemetry, ...) need no call: every client
    // already receives every event. Answer so callers can treat it as a no-op.
    return { jsonrpc: '2.0', id, result: { subscribed: true, events: 'all' } }
  }

  let args: unknown[]
  try {
    args = method.args(params)
  } catch (e) {
    return error(id, RPC_ERROR.invalidParams, (e as Error).message)
  }
  const evt = syntheticEvent(client)
  try {
    if (method.transport === 'invoke') {
      const result = await invokeChannel(method.channel, evt, ...args)
      return { jsonrpc: '2.0', id, result: result === undefined ? null : result }
    }
    const ok = emitChannel(method.channel, evt, ...args)
    if (!ok) return error(id, RPC_ERROR.methodNotFound, `no listener for '${method.channel}'`)
    return req.id === undefined ? undefined : { jsonrpc: '2.0', id, result: null }
  } catch (e) {
    return error(id, RPC_ERROR.internal, (e as Error).message ?? String(e))
  }
}

function onData(client: Client, chunk: Buffer | string): void {
  client.buffer += typeof chunk === 'string' ? chunk : chunk.toString('utf8')
  if (client.buffer.length > 4 * 1024 * 1024) {
    log(`[ui-bridge] client ${client.id} sent an oversized frame, closing`)
    client.socket.destroy()
    return
  }
  const { messages, rest } = decodeFrames(client.buffer)
  client.buffer = rest
  for (const msg of messages) {
    if (!isRpcRequest(msg)) {
      const id =
        msg && typeof msg === 'object' && 'id' in msg ? ((msg as RpcRequest).id ?? null) : null
      write(client, error(id, RPC_ERROR.invalidRequest, 'expected a JSON-RPC 2.0 request'))
      continue
    }
    void dispatch(client, msg).then((res) => {
      if (res) write(client, res)
    })
  }
}

export function startUiBridge(opts: UiBridgeOptions = {}): Promise<string> {
  if (server) return Promise.resolve(socketPath as string)
  log = opts.log ?? log
  methods = buildMethods()
  const path = opts.socketPath ?? defaultSocketPath()
  if (existsSync(path)) {
    try {
      unlinkSync(path)
    } catch {
      /* stale socket from a previous run */
    }
  }
  return new Promise((resolve, reject) => {
    const srv = createServer((socket) => {
      const client: Client = { id: nextClientId++, socket, buffer: '' }
      clients.add(client)
      log(`[ui-bridge] client ${client.id} connected (${clients.size} total)`)
      socket.on('data', (chunk) => onData(client, chunk))
      socket.on('error', (e) => log(`[ui-bridge] client ${client.id}: ${e.message}`))
      socket.on('close', () => {
        clients.delete(client)
        log(`[ui-bridge] client ${client.id} disconnected (${clients.size} left)`)
      })
    })
    srv.on('error', (e) => {
      log(`[ui-bridge] server error: ${e.message}`)
      if (!server) reject(e)
    })
    srv.listen(path, () => {
      server = srv
      socketPath = path
      log(`[ui-bridge] listening on ${path} (${methods?.size ?? 0} methods)`)
      resolve(path)
    })
  })
}

export function stopUiBridge(): void {
  for (const c of clients) c.socket.destroy()
  clients.clear()
  server?.close()
  server = undefined
  if (socketPath && existsSync(socketPath)) {
    try {
      unlinkSync(socketPath)
    } catch {
      /* already gone */
    }
  }
  socketPath = undefined
}
