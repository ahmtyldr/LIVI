import { PhoneType } from '@shared/types/Config'
import { Message } from './readable.js'

/**
 * Events only the CarlinKit dongle emits: box lifecycle, firmware update,
 * dongle-side Bluetooth pairing and GNSS. Dropping dongle support removes
 * this file together with driver/dongle.
 */

export class DongleReady extends Message {}

export class ManufacturerInfo extends Message {
  a: number
  b: number

  constructor(a: number, b: number) {
    super()
    this.a = a
    this.b = b
  }
}

export class SoftwareVersion extends Message {
  version: string

  constructor(version: string) {
    super()
    this.version = version
  }
}

export class GnssData extends Message {
  text: string

  constructor(text: string) {
    super()
    this.text = text
  }
}

export class BluetoothAddress extends Message {
  address: string

  constructor(address: string) {
    super()
    this.address = address
  }
}

export class BluetoothPIN extends Message {
  pin: string

  constructor(pin: string) {
    super()
    this.pin = pin
  }
}

export class BluetoothDeviceName extends Message {
  name: string

  constructor(name: string) {
    super()
    this.name = name
  }
}

export class WifiDeviceName extends Message {
  name: string

  constructor(name: string) {
    super()
    this.name = name
  }
}

export class HiCarLink extends Message {
  link: string

  constructor(link: string) {
    super()
    this.link = link
  }
}

export class BluetoothPairedList extends Message {
  data: string

  constructor(data: string) {
    super()
    this.data = data
  }
}

export class Plugged extends Message {
  phoneType: PhoneType
  wifi?: number

  constructor(phoneType: PhoneType, wifi?: number) {
    super()
    this.phoneType = phoneType
    this.wifi = wifi
  }
}

export class Unplugged extends Message {}

export class BluetoothPeerConnecting extends Message {
  address: string

  constructor(address: string) {
    super()
    this.address = address
  }
}

export class BluetoothPeerConnected extends Message {
  address: string

  constructor(address: string) {
    super()
    this.address = address
  }
}

export type OpenedFields = {
  width: number
  height: number
  fps: number
  format: number
  packetMax: number
  iBox: number
  phoneMode: number
}

export class Opened extends Message {
  width: number
  height: number
  fps: number
  format: number
  packetMax: number
  iBox: number
  phoneMode: number

  constructor(fields: OpenedFields) {
    super()
    this.width = fields.width
    this.height = fields.height
    this.fps = fields.fps
    this.format = fields.format
    this.packetMax = fields.packetMax
    this.iBox = fields.iBox
    this.phoneMode = fields.phoneMode
  }
}

export type BoxDeviceEntry = {
  id?: string
  type?: string
  name?: string
  index?: string | number
  time?: string
  rfcomm?: string | number
  source?: 'dongle' | 'host'
} & Record<string, unknown>

export type BoxInfoSettings = {
  uuid?: string
  MFD?: string
  boxType?: string
  OemName?: string
  productType?: string
  HiCar?: number
  supportLinkType?: string
  supportFeatures?: string
  hwVersion?: string
  wifiChannel?: number
  CusCode?: string
  DevList?: BoxDeviceEntry[]
} & Record<string, unknown>

export class BoxInfo extends Message {
  settings: BoxInfoSettings

  constructor(settings: BoxInfoSettings) {
    super()
    this.settings = settings
  }
}

export class VendorSessionInfo extends Message {
  public readonly raw: Buffer

  public constructor(raw: Buffer) {
    super()
    this.raw = raw
  }
}

export class Phase extends Message {
  value: number

  constructor(value: number) {
    super()
    this.value = value
  }
}

export enum BoxPhase {
  EVT_ANDROID_PLUG_OUT = 0,
  EVT_ANDROID_PLUG_IN = 1,
  EVT_IPHONE_PLUG_OUT = 2,
  EVT_IPHONE_PLUG_IN = 3,

  EVT_PHONE_PLUG_IN = 4,
  EVT_WAIT_HOTPOT = 5,
  EVT_WAIT_AIRPLAY = 6,
  EVT_PERMMISION_ASKING = 7,
  EVT_NOT_REGIST = 8,
  EVT_REG = 9,
  EVT_SCREEN_ON = 10,
  EVT_SCREEN_OFF = 11,

  EVT_OTG_PLUG_OUT = 12,
  EVT_OTG_PLUG_IN = 13,

  EVT_ANDROID_WORKING = 14,
  EVT_IPHONE_WORKING = 15,
  EVT_CARLIFE_DOWNLOAD = 16,
  EVT_SET_PERMISSION = 17,

  EVT_DECODE_CONFIGURE_ERR = 18,
  EVT_DECODE_OUTPUT_ERR = 19,

  EVT_SETTINGS_PAGE_ENTER = 20,
  EVT_SETTINGS_PAGE_BACK = 21,

  EVT_FAKE_OTG_PLUG_IN = 22,
  EVT_FAKE_OTG_PLUG_OUT = 23,

  EVT_BOX_ENTER_U_MODE = 24,
  EVT_MANUAL_DISCONNECT_PHONE = 25,

  EVT_BOX_READY = 116,

  EVT_BOXMIC_DETECTED = 117,
  EVT_BOXMIC_CONNECTED = 118,
  EVT_BOXMIC_DISCONNECTED = 119,

  EVT_BOX_UPDATE = 120,
  EVT_BOX_UPDATE_SUCCESS = 121,
  EVT_BOX_UPDATE_FAILED = 122,
  EVT_BOX_VERSION_ERROR = 123,
  EVT_BOX_VERSION_SHOW = 124,

  EVT_BOX_OTA_UPDATE = 125,
  EVT_BOX_OTA_UPDATE_SUCCESS = 126,
  EVT_BOX_OTA_UPDATE_FAILED = 127,

  EVT_BOX_SUPPORT_AUTO_CONNECT = 200,
  EVT_BOX_SCANING_DEVICES = 201,
  EVT_BOX_DEVICE_FOUND = 202,
  EVT_BOX_DEVICE_NOT_FOUND = 203,
  EVT_BOX_CONNECT_DEVICE_FAILED = 204,

  EVT_BOX_BLUETOOTH_CONNECTED = 205,
  EVT_BOX_BLUETOOTH_DISCONNECTED = 206,

  EVT_BOX_WIFI_CONNECTED = 207,
  EVT_BOX_WIFI_DISCONNECTED = 208,

  EVT_BOX_BLUETOOTH_PAIR_START = 209,
  EVT_UPDATE_BLUETOOTH_PAIRED_LIST = 210,
  EVT_UPDATE_BLUETOOTH_ONLINE_LIST = 211,

  EVT_BOX_REQUEST_VIDEO_FOCUS = 212,
  EVT_BOX_RELEASE_VIDEO_FOCUS = 213,

  EVT_UPDATE_CONNECTION_URL = 214
}

export function boxPhaseToString(v: number): string {
  const byValue = BoxPhase as unknown as Record<number, string>
  return byValue[v] ?? `UNKNOWN_PHASE_${v}`
}

export enum BoxUpdateStatus {
  BoxUpdateStart = 1,
  BoxUpdateSuccess = 2,
  BoxUpdateFailed = 3,

  BoxOtaUpdateStart = 5,
  BoxOtaUpdateSuccess = 6,
  BoxOtaUpdateFailed = 7
}

export function boxUpdateStatusToString(status: number): string {
  switch (status) {
    case BoxUpdateStatus.BoxUpdateStart:
      return 'EVT_BOX_UPDATE'
    case BoxUpdateStatus.BoxUpdateSuccess:
      return 'EVT_BOX_UPDATE_SUCCESS'
    case BoxUpdateStatus.BoxUpdateFailed:
      return 'EVT_BOX_UPDATE_FAILED'
    case BoxUpdateStatus.BoxOtaUpdateStart:
      return 'EVT_BOX_OTA_UPDATE'
    case BoxUpdateStatus.BoxOtaUpdateSuccess:
      return 'EVT_BOX_OTA_UPDATE_SUCCESS'
    case BoxUpdateStatus.BoxOtaUpdateFailed:
      return 'EVT_BOX_OTA_UPDATE_FAILED'
    default:
      return `EVT_BOX_UPDATE_UNKNOWN(${status})`
  }
}

export class BoxUpdateProgress extends Message {
  progress: number

  constructor(progress: number) {
    super()
    this.progress = progress
  }
}

export class BoxUpdateState extends Message {
  status: BoxUpdateStatus | number
  statusText: string
  isOta: boolean
  isTerminal: boolean
  ok?: boolean

  constructor(raw: number) {
    super()
    this.status = raw
    this.statusText = boxUpdateStatusToString(raw)
    this.isOta = raw === 5 || raw === 6 || raw === 7
    this.isTerminal = raw === 2 || raw === 3 || raw === 6 || raw === 7

    if (raw === 2 || raw === 6) this.ok = true
    if (raw === 3 || raw === 7) this.ok = false
  }
}
