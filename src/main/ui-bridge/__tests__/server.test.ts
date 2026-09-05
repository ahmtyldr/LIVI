import { mkdtempSync } from 'node:fs'
import { connect, type Socket } from 'node:net'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { registerIpcHandle, registerIpcOn } from '@main/ipc/register'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { decodeFrames, encodeFrame } from '../protocol'
import { bridgeEmit, startUiBridge, stopUiBridge } from '../server'

type Frame = {
  id?: number
  result?: unknown
  error?: { code: number; message: string }
  method?: string
  params?: { channel: string; args: unknown[] }
}

function client(
  path: string
): Promise<{ socket: Socket; frames: Frame[]; next: () => Promise<Frame> }> {
  return new Promise((resolve, reject) => {
    const frames: Frame[] = []
    const waiters: ((f: Frame) => void)[] = []
    let buffer = ''
    const socket = connect(path)
    socket.on('error', reject)
    socket.on('data', (chunk) => {
      buffer += chunk.toString('utf8')
      const { messages, rest } = decodeFrames(buffer)
      buffer = rest
      for (const m of messages) {
        const f = m as Frame
        const w = waiters.shift()
        if (w) w(f)
        else frames.push(f)
      }
    })
    socket.on('connect', () =>
      resolve({
        socket,
        frames,
        next: () =>
          new Promise<Frame>((res) => {
            const f = frames.shift()
            if (f) res(f)
            else waiters.push(res)
          })
      })
    )
  })
}

describe('ui-bridge server', () => {
  let path: string
  beforeEach(async () => {
    path = join(mkdtempSync(join(tmpdir(), 'livi-ui-')), 'ui.sock')
    await startUiBridge({ socketPath: path, log: () => {} })
  })
  afterEach(() => stopUiBridge())

  it('answers ping and describe', async () => {
    const c = await client(path)
    c.socket.write(encodeFrame({ jsonrpc: '2.0', id: 1, method: 'rpc.ping' }))
    expect((await c.next()).result).toBe('pong')
    c.socket.write(encodeFrame({ jsonrpc: '2.0', id: 2, method: 'rpc.describe' }))
    const d = (await c.next()).result as { methods: unknown[]; events: string[] }
    expect(d.methods.length).toBeGreaterThan(60)
    expect(d.events).toContain('usb-event')
    c.socket.end()
  })

  it('dispatches invoke calls to the registered IPC handler', async () => {
    registerIpcHandle('getSettings', async () => ({ darkMode: true, carName: 'test' }))
    const c = await client(path)
    c.socket.write(
      encodeFrame({ jsonrpc: '2.0', id: 1, method: 'projection.settings.get', params: [] })
    )
    expect((await c.next()).result).toEqual({ darkMode: true, carName: 'test' })
    c.socket.end()
  })

  it('maps send calls through the preload argument shape', async () => {
    const seen: unknown[] = []
    registerIpcOn('projection-touch', (_evt, data: unknown) => {
      seen.push(data)
    })
    const c = await client(path)
    c.socket.write(
      encodeFrame({
        jsonrpc: '2.0',
        id: 1,
        method: 'projection.ipc.sendTouch',
        params: [10, 20, 1]
      })
    )
    expect((await c.next()).result).toBeNull()
    expect(seen).toEqual([{ x: 10, y: 20, action: 1 }])
    c.socket.end()
  })

  it('reports unknown methods and handler errors as JSON-RPC errors', async () => {
    registerIpcHandle('projection-start', async () => {
      throw new Error('boom')
    })
    const c = await client(path)
    c.socket.write(encodeFrame({ jsonrpc: '2.0', id: 1, method: 'nope.nothing' }))
    expect((await c.next()).error?.code).toBe(-32601)
    c.socket.write(encodeFrame({ jsonrpc: '2.0', id: 2, method: 'projection.ipc.start' }))
    const err = (await c.next()).error
    expect(err?.code).toBe(-32603)
    expect(err?.message).toBe('boom')
    c.socket.end()
  })

  it('broadcasts renderer events to every client, binary as $bytes', async () => {
    const a = await client(path)
    const b = await client(path)
    bridgeEmit('telemetry:update', { speedKph: 42 })
    bridgeEmit('projection-audio-chunk', Buffer.from([1, 2, 3]))
    const a1 = await a.next()
    const b1 = await b.next()
    expect(a1.method).toBe('event')
    expect(a1.params).toEqual({ channel: 'telemetry:update', args: [{ speedKph: 42 }] })
    expect(b1.params?.channel).toBe('telemetry:update')
    const a2 = await a.next()
    expect(a2.params?.channel).toBe('projection-audio-chunk')
    expect(Buffer.isBuffer(a2.params?.args[0])).toBe(true)
    expect(Buffer.from(a2.params?.args[0] as Buffer)).toEqual(Buffer.from([1, 2, 3]))
    a.socket.end()
    b.socket.end()
  })

  it('answers local values without IPC', async () => {
    const c = await client(path)
    c.socket.write(encodeFrame({ jsonrpc: '2.0', id: 1, method: 'app.platform' }))
    expect((await c.next()).result).toBe(process.platform)
    c.socket.end()
  })
})
