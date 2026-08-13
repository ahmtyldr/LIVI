import { decodeTypeMap } from '@shared/types/AudioDecode'
import { PhoneType } from '@shared/types/Config'
import type { NaviInfo } from '@shared/types/NavigationTypes'
import type { AudioCommand, CommandMapping } from '@shared/types/ProjectionEnums'

/**
 * Internal projection event model. Every driver (CarPlay, Android Auto,
 * dongle) emits these; wire parsing lives with the driver that owns the wire.
 */
export abstract class Message {}

export class DuckAudio extends Message {
  level: number
  durationMs: number

  constructor(level: number, durationMs: number) {
    super()
    this.level = Math.max(0, Math.min(1, level))
    this.durationMs = Math.max(0, durationMs)
  }
}

export class Command extends Message {
  value: CommandMapping

  constructor(value: CommandMapping) {
    super()
    this.value = value
  }
}

export { PhoneType }

export type AudioDataFields = {
  decodeType: number
  audioType: number
  volume?: number
  command?: AudioCommand
  volumeDuration?: number
  data?: Int16Array
  sampleRate?: number
  channels?: number
}

export class AudioData extends Message {
  command?: AudioCommand
  decodeType: number
  sampleRate: number
  channels: number
  volume: number
  volumeDuration?: number
  audioType: number
  data?: Int16Array

  constructor(fields: AudioDataFields) {
    super()
    this.decodeType = fields.decodeType
    const fmt = decodeTypeMap[fields.decodeType]
    this.sampleRate = fields.sampleRate ?? fmt?.frequency ?? 48000
    this.channels = fields.channels ?? fmt?.channel ?? 2
    this.volume = fields.volume ?? 0
    this.audioType = fields.audioType
    this.command = fields.command
    this.volumeDuration = fields.volumeDuration
    this.data = fields.data
  }
}

export type VideoDataFields = {
  width: number
  height: number
  data: Buffer
  cluster?: boolean
  flags?: number
  unknown?: number
}

export class VideoData extends Message {
  width: number
  height: number
  flags: number
  length: number
  unknown: number
  data: Buffer
  cluster: boolean

  constructor(fields: VideoDataFields) {
    super()
    this.width = fields.width
    this.height = fields.height
    this.flags = fields.flags ?? 0
    this.length = fields.data.length
    this.unknown = fields.unknown ?? 0
    this.data = fields.data
    this.cluster = fields.cluster ?? false
  }
}

export enum MediaType {
  Data = 1,
  AlbumCover = 2,
  AlbumCoverAlt = 3,
  ControlAutoplayTrigger = 100
}

export enum NavigationMetaType {
  DashboardInfo = 200,
  DashboardImage = 201
}

export type MediaInfo = {
  MediaSongName?: string
  MediaAlbumName?: string
  MediaArtistName?: string
  MediaAPPName?: string
  MediaSongDuration?: number
  MediaSongPlayTime?: number
  MediaPlayStatus?: number
} & Record<string, unknown>

export type MediaPayload =
  | { type: MediaType.Data; media: MediaInfo }
  | { type: MediaType.AlbumCoverAlt; base64Image: string }
  | { type: MediaType.ControlAutoplayTrigger }

export class MediaData extends Message {
  mediaType: MediaType
  payload?: MediaPayload

  constructor(mediaType: MediaType, payload?: MediaPayload) {
    super()
    this.mediaType = mediaType
    this.payload = payload
  }
}

export type { NaviInfo } from '@shared/types/NavigationTypes'

export class NavigationData extends Message {
  metaType: NavigationMetaType
  navi: NaviInfo | null
  rawUtf8: string

  constructor(metaType: NavigationMetaType, navi: NaviInfo | null, rawUtf8 = '') {
    super()
    this.metaType = metaType
    this.navi = navi
    this.rawUtf8 = rawUtf8
  }
}
