import { AudioCommand } from '@shared/types/ProjectionEnums'
import { afterEach, describe, expect, test, vi } from 'vitest'

const net = vi.hoisted(() => {
  const { EventEmitter } = require('node:events')
  const sockets: any[] = []
  class FakeSocket extends EventEmitter {
    written: Buffer[] = []
    destroyed = false
    constructor() {
      super()
      sockets.push(this)
    }
    write(b: Buffer): void {
      this.written.push(b)
    }
    destroy(): void {
      this.destroyed = true
      this.emit('close')
    }
  }
  return { sockets, connect: vi.fn(() => new FakeSocket()) }
})

const mic = vi.hoisted(() => {
  const { EventEmitter } = require('node:events')
  const mics: any[] = []
  class FakeMic extends EventEmitter {
    device?: string
    started?: number
    stopped = false
    constructor() {
      super()
      mics.push(this)
    }
    setDevice(d?: string): void {
      this.device = d
    }
    start(decode: number): void {
      this.started = decode
    }
    stop(): void {
      this.stopped = true
    }
  }
  return { mics, FakeMic }
})

vi.mock('node:net', () => ({ connect: net.connect }))
vi.mock('@main/services/audio/Microphone', () => ({ default: mic.FakeMic }))

import { ScoAudio } from '../ScoAudio'

describe('ScoAudio', () => {
  afterEach(() => {
    net.sockets.length = 0
    mic.mics.length = 0
    vi.clearAllMocks()
  })

  test('on connect emits phonecall start and starts the mic; a full frame emits PCM', () => {
    const emitted: any[] = []
    const sco = new ScoAudio({ emitAudio: (m) => emitted.push(m), getMicDevice: () => 'mic0' })
    sco.start()
    const sock = net.sockets[0]
    sock.emit('connect')

    expect(emitted[0].command).toBe(AudioCommand.AudioPhonecallStart)
    expect(mic.mics[0].started).toBe(3)
    expect(mic.mics[0].device).toBe('mic0')

    mic.mics[0].emit('data', Buffer.alloc(160))
    expect(sock.written.length).toBe(1)

    emitted.length = 0
    sock.emit('data', Buffer.alloc(320))
    expect(emitted).toHaveLength(1)
    expect(emitted[0].data).toBeInstanceOf(Int16Array)
    expect(emitted[0].data.length).toBe(160)
  })

  test('partial data is buffered until a full frame arrives', () => {
    const emitted: any[] = []
    const sco = new ScoAudio({ emitAudio: (m) => emitted.push(m), getMicDevice: () => undefined })
    sco.start()
    net.sockets[0].emit('connect')
    emitted.length = 0
    net.sockets[0].emit('data', Buffer.alloc(200))
    expect(emitted).toHaveLength(0)
    net.sockets[0].emit('data', Buffer.alloc(200))
    expect(emitted).toHaveLength(1)
  })

  test('stop tears down the mic and emits phonecall stop', () => {
    const emitted: any[] = []
    const sco = new ScoAudio({ emitAudio: (m) => emitted.push(m), getMicDevice: () => undefined })
    sco.start()
    net.sockets[0].emit('connect')
    emitted.length = 0
    sco.stop()
    expect(mic.mics[0].stopped).toBe(true)
    expect(net.sockets[0].destroyed).toBe(true)
    expect(emitted.at(-1).command).toBe(AudioCommand.AudioPhonecallStop)
  })

  test('a socket error stops cleanly', () => {
    const sco = new ScoAudio({ emitAudio: () => {}, getMicDevice: () => undefined })
    sco.start()
    net.sockets[0].emit('connect')
    net.sockets[0].emit('error', new Error('boom'))
    expect(mic.mics[0].stopped).toBe(true)
  })

  test('stale socket data and mic frames after stop are ignored', () => {
    const emitted: any[] = []
    const sco = new ScoAudio({ emitAudio: (m) => emitted.push(m), getMicDevice: () => undefined })
    sco.start()
    const sock = net.sockets[0]
    sock.emit('connect')
    const m = mic.mics[0]
    sco.stop()
    emitted.length = 0
    sock.written.length = 0
    // Both sides are detached; late events must not route anywhere.
    sock.emit('data', Buffer.alloc(320))
    m.emit('data', Buffer.alloc(160))
    expect(emitted).toHaveLength(0)
    expect(sock.written).toHaveLength(0)
  })

  test('start is idempotent', () => {
    const sco = new ScoAudio({ emitAudio: () => {}, getMicDevice: () => undefined })
    sco.start()
    sco.start()
    expect(net.sockets).toHaveLength(1)
  })

  test('stop before connect is a no-op', () => {
    const emitted: any[] = []
    const sco = new ScoAudio({ emitAudio: (m) => emitted.push(m), getMicDevice: () => undefined })
    sco.stop()
    expect(emitted).toHaveLength(0)
  })
})
