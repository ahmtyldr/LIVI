// Wire format of the UI bridge: newline-delimited JSON-RPC 2.0 over a Unix
// socket. Binary payloads (PCM chunks) travel as { "$bytes": <base64> }.

export type RpcId = number | string | null

export type RpcRequest = {
  jsonrpc: '2.0'
  id?: RpcId
  method: string
  params?: unknown[] | Record<string, unknown>
}

export type RpcResponse =
  | { jsonrpc: '2.0'; id: RpcId; result: unknown }
  | { jsonrpc: '2.0'; id: RpcId; error: { code: number; message: string; data?: unknown } }

/** Server → client notification carrying a renderer event. */
export type RpcEvent = {
  jsonrpc: '2.0'
  method: 'event'
  params: { channel: string; args: unknown[] }
}

export const RPC_ERROR = {
  parse: -32700,
  invalidRequest: -32600,
  methodNotFound: -32601,
  invalidParams: -32602,
  internal: -32603
} as const

/** Converts binary values to `{ $bytes }` before serialisation. Done as a
 *  walk rather than a JSON.stringify replacer because Buffer#toJSON runs
 *  first and would hand the replacer `{ type: 'Buffer', data: [...] }`. */
function toWire(value: unknown, depth = 0): unknown {
  if (value == null || typeof value !== 'object') {
    return typeof value === 'bigint' ? Number(value) : value
  }
  if (Buffer.isBuffer(value)) return { $bytes: value.toString('base64') }
  if (value instanceof ArrayBuffer) return { $bytes: Buffer.from(value).toString('base64') }
  if (ArrayBuffer.isView(value)) {
    return {
      $bytes: Buffer.from(value.buffer, value.byteOffset, value.byteLength).toString('base64')
    }
  }
  if (depth > 32) return value
  if (Array.isArray(value)) return value.map((v) => toWire(v, depth + 1))
  if (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null) {
    const out: Record<string, unknown> = {}
    for (const [k, v] of Object.entries(value)) out[k] = toWire(v, depth + 1)
    return out
  }
  return value
}

/** One frame: compact JSON plus the trailing newline. */
export function encodeFrame(message: unknown): string {
  return `${JSON.stringify(toWire(message))}\n`
}

function reviver(_key: string, value: unknown): unknown {
  if (
    value &&
    typeof value === 'object' &&
    '$bytes' in value &&
    typeof (value as { $bytes: unknown }).$bytes === 'string'
  ) {
    return Buffer.from((value as { $bytes: string }).$bytes, 'base64')
  }
  return value
}

/** Splits buffered socket data into complete frames; returns the parsed
 *  messages and whatever partial line is left for the next chunk. */
export function decodeFrames(buffered: string): { messages: unknown[]; rest: string } {
  const lines = buffered.split('\n')
  const rest = lines.pop() ?? ''
  const messages: unknown[] = []
  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue
    try {
      messages.push(JSON.parse(trimmed, reviver))
    } catch {
      messages.push({ $parseError: trimmed.slice(0, 200) })
    }
  }
  return { messages, rest }
}

export function isRpcRequest(value: unknown): value is RpcRequest {
  return (
    !!value &&
    typeof value === 'object' &&
    (value as RpcRequest).jsonrpc === '2.0' &&
    typeof (value as RpcRequest).method === 'string'
  )
}
