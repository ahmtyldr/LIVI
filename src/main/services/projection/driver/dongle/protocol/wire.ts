/** CarlinKit dongle wire framing: 16-byte header with magic, length and type. */

export enum MessageType {
  Open = 0x01,
  Plugged = 0x02,
  Phase = 0x03,
  Unplugged = 0x04,
  Touch = 0x05,
  VideoData = 0x06,
  AudioData = 0x07,
  Command = 0x08,
  LogoType = 0x09,
  BluetoothAddress = 0x0a,
  CarplayControl = 0x0b,
  BluetoothPIN = 0x0c,
  BluetoothDeviceName = 0x0d,
  WifiDeviceName = 0x0e,
  DisconnectPhone = 0x0f,
  DashboardData = 0x10,
  WifiStatusData = 0x11,
  BluetoothPairedList = 0x12,
  DiskInfo = 0x13,
  ManufacturerInfo = 0x14,
  CloseDongle = 0x15,
  CameraInfo = 0x16,
  MultiTouch = 0x17,
  HiCarLink = 0x18,
  BoxSettings = 0x19,
  ForgetBluetoothAddr = 0x22,
  PeerBluetoothAddress = 0x23,
  PeerBluetoothAddressAlt = 0x24,
  UiHidePeerInfo = 0x25,
  UiBringToForeground = 0x26,
  GnssData = 0x29,
  MetaData = 0x2a,
  ShowPinCode = 0x2b,
  ClusterVideoData = 0x2c,
  ClusterFocusRequest = 0x6e,
  ClusterFocusRelease = 0x6f,
  FactorySetting = 0x77,
  DebugTest = 0x88,
  SendFile = 0x99,
  ExtendedManufacturerInfo = 0xa1,
  VendorSessionInfo = 0xa3,
  HeartBeat = 0xaa,
  UpdateProgress = 0xb1,
  UpdateState = 0xbb,
  SoftwareVersion = 0xcc,
  EnableCrypt = 0xf0,
  DebugTrace = 0xff,
  DuckAudio = 0x1000
}

export class HeaderBuildError extends Error {}

export class MessageHeader {
  length: number
  type: MessageType

  public constructor(length: number, type: MessageType) {
    this.length = length
    this.type = type
  }

  static fromBuffer(data: Buffer): MessageHeader {
    if (data.length !== 16) {
      throw new HeaderBuildError(`Invalid buffer size - Expecting 16, got ${data.length}`)
    }
    const magic = data.readUInt32LE(0)
    if (magic !== MessageHeader.magic) {
      throw new HeaderBuildError(`Invalid magic number, received ${magic}`)
    }
    const length = data.readUInt32LE(4)
    const msgType: MessageType = data.readUInt32LE(8)
    const typeCheck = data.readUInt32LE(12)
    if (typeCheck != ((msgType ^ -1) & 0xffffffff) >>> 0) {
      throw new HeaderBuildError(`Invalid type check, received ${typeCheck}`)
    }
    return new MessageHeader(length, msgType)
  }

  static asBuffer(messageType: MessageType, byteLength: number): Buffer {
    const dataLen = Buffer.alloc(4)
    dataLen.writeUInt32LE(byteLength)
    const type = Buffer.alloc(4)
    type.writeUInt32LE(messageType)
    const typeCheck = Buffer.alloc(4)
    typeCheck.writeUInt32LE(((messageType ^ -1) & 0xffffffff) >>> 0)
    const magicNumber = Buffer.alloc(4)
    magicNumber.writeUInt32LE(MessageHeader.magic)
    return Buffer.concat([magicNumber, dataLen, type, typeCheck])
  }

  static dataLength = 16
  static magic = 0x55aa55aa
}
