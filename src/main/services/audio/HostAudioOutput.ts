import { gstHost } from '@main/services/video/gstHost'

export type HostAudioOutputOptions = {
  sampleRate: number
  channels: number
  device?: string
  /** Voice and call streams take the short path to the sink. */
  realtime?: boolean
  /** The host's stream id, once the stream is open. */
  onOpened?: (streamId: number) => void
}

/** Buffers held while the stream is still opening. */
const PENDING_MAX = 64

/**
 * One audio stream played out by the gst-host. The samples are handed over as
 * they are, the pipeline and the sink live in the host.
 */
export class HostAudioOutput {
  private streamId: number | null = null
  private opening = false
  private stopped = false
  private readonly pending: Buffer[] = []

  constructor(private readonly opts: HostAudioOutputOptions) {}

  get hostStreamId(): number | null {
    return this.streamId
  }

  start(): void {
    if (this.opening || this.streamId != null) return
    this.opening = true
    void gstHost
      .openAudio(Buffer.alloc(32), {
        codec: 'pcm-le',
        payloadType: 0,
        clockRate: this.opts.sampleRate,
        channels: this.opts.channels,
        latencyMs: 0,
        realtime: this.opts.realtime ?? false,
        fed: true,
        device: this.opts.device
      })
      .then(({ streamId }) => {
        this.opening = false
        if (this.stopped) {
          gstHost.closeAudio(streamId)
          return
        }
        this.streamId = streamId
        gstHost.setAudioActive(streamId, true)
        for (const b of this.pending) gstHost.pushAudio(streamId, b)
        this.pending.length = 0
        this.opts.onOpened?.(streamId)
      })
      .catch(() => {
        this.opening = false
      })
  }

  write(chunk: Int16Array | Buffer | undefined | null): void {
    if (!chunk || this.stopped) return
    const buf = Buffer.isBuffer(chunk)
      ? chunk
      : Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength)

    if (this.streamId == null) {
      if (this.pending.length < PENDING_MAX) this.pending.push(buf)
      return
    }
    gstHost.pushAudio(this.streamId, buf)
  }

  stop(): void {
    this.stopped = true
    this.pending.length = 0
    if (this.streamId != null) {
      gstHost.closeAudio(this.streamId)
      this.streamId = null
    }
  }
}
