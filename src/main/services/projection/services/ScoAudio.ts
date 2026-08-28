import { connect, type Socket } from 'node:net'
import Microphone from '@main/services/audio/Microphone'
import { AudioCommand } from '@shared/types/ProjectionEnums'
import { AudioData } from '../messages'

const SOCK_PATH = '/tmp/aa-sco.sock'
// 20 ms at 8 kHz mono s16le.
const CHUNK_BYTES = 320
const SCO_AUDIO_TYPE = 2
const SCO_DECODE_TYPE = 3

// HFP call audio, PCM duplex with the helper's SCO bridge (/tmp/aa-sco.sock).
// Downlink feeds ProjectionAudio as the call stream (volume/ducking apply),
// uplink captures the configured mic at 8 kHz.
export class ScoAudio {
  private sock: Socket | null = null
  private mic: Microphone | null = null
  private rx: Buffer = Buffer.alloc(0)

  constructor(
    private readonly deps: {
      emitAudio: (msg: AudioData) => void
      getMicDevice: () => string | undefined
    }
  ) {}

  start(): void {
    if (this.sock) return
    const sock = connect(SOCK_PATH)
    this.sock = sock
    sock.on('connect', () => {
      console.log('[ScoAudio] attached to SCO bridge')
      this.deps.emitAudio(
        new AudioData({
          audioType: SCO_AUDIO_TYPE,
          decodeType: SCO_DECODE_TYPE,
          command: AudioCommand.AudioPhonecallStart
        })
      )
      const mic = new Microphone()
      this.mic = mic
      mic.setDevice(this.deps.getMicDevice())
      mic.on('data', (chunk: Buffer) => {
        if (this.sock === sock) sock.write(chunk)
      })
      mic.start(SCO_DECODE_TYPE)
    })
    sock.on('data', (chunk: Buffer) => {
      if (this.sock !== sock) return
      this.rx = Buffer.concat([this.rx, chunk])
      while (this.rx.length >= CHUNK_BYTES) {
        const part = this.rx.subarray(0, CHUNK_BYTES)
        this.rx = this.rx.subarray(CHUNK_BYTES)
        const pcm = new Int16Array(
          part.buffer.slice(part.byteOffset, part.byteOffset + part.byteLength)
        )
        this.deps.emitAudio(
          new AudioData({ audioType: SCO_AUDIO_TYPE, decodeType: SCO_DECODE_TYPE, data: pcm })
        )
      }
    })
    sock.on('error', () => this.stop())
    sock.on('close', () => this.stop())
  }

  stop(): void {
    if (!this.sock && !this.mic) return
    this.mic?.stop()
    this.mic = null
    const s = this.sock
    this.sock = null
    this.rx = Buffer.alloc(0)
    s?.destroy()
    this.deps.emitAudio(
      new AudioData({
        audioType: SCO_AUDIO_TYPE,
        decodeType: SCO_DECODE_TYPE,
        command: AudioCommand.AudioPhonecallStop
      })
    )
  }
}
