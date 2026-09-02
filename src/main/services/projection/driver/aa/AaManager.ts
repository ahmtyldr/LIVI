/**
 * AaManager, shared Android Auto infrastructure (singleton).
 *
 * Owns the wireless sessions the helper announces once it carries TCP and TLS,
 * and the wired AOAP bring-up (one UsbAoapBridge per device). Every helper
 * session, and every wired loopback socket after the AOAP handshake, spawns ONE
 * AaSession handed off via onSpawn. Holds the codec-capability seed applied to
 * each new AaSession.
 */

import * as net from 'node:net'
import type { Config } from '@shared/types'
import type { AaMediaSinkDeps } from './AaEventBridge'
import { AaSession, type AaSessionSeed } from './AaSession'
import { AOAP_LOOPBACK_PORT } from './stack/aoap/constants'
import { HelperSessionLink } from './stack/transport/HelperSessionLink'
import { UsbAoapBridge } from './stack/transport/UsbAoapBridge'

type Device = USBDevice

export type HelperSessionEvent = { event: string; socket?: string; peer?: string }

/** The helper's event stream, where new phone sessions are announced. */
export type HelperSessionSource = {
  subscribe(onEvent: (ev: HelperSessionEvent) => void, onClose?: () => void): { close: () => void }
}

const HELPER_RESUBSCRIBE_MS = 2000

export interface AaManagerOptions {
  getConfig: () => Config
  onWillReenumerate?: (durationMs: number) => void
  onSpawn: (session: AaSession) => void
  mediaSink?: AaMediaSinkDeps
}

function deviceKey(device: Device): string {
  const serial = device.serialNumber ?? ''
  if (serial) return `serial:${serial}`
  const vid = device.vendorId ?? 0
  const pid = device.productId ?? 0
  return `${vid}:${pid}`
}

export class AaManager {
  private _helper: HelperSessionSource | null = null
  private _helperSub: { close: () => void } | null = null
  private readonly _wiredBridges = new Map<string, UsbAoapBridge>()
  private readonly _sessions = new Set<AaSession>()
  private readonly _wirelessPeer = new Map<AaSession, string>()

  private _hevcSupported = false
  private _vp9Supported = false
  private _av1Supported = false
  private _initialNightMode: boolean | undefined = undefined
  private _clusterStreamActive = true

  private readonly _getConfig: () => Config
  private readonly _onWillReenumerate: ((durationMs: number) => void) | undefined
  private readonly _onSpawn: (session: AaSession) => void
  private readonly _mediaSink: AaMediaSinkDeps | undefined

  constructor(opts: AaManagerOptions) {
    this._getConfig = opts.getConfig
    this._onWillReenumerate = opts.onWillReenumerate
    this._onSpawn = opts.onSpawn
    this._mediaSink = opts.mediaSink
  }

  setHevcSupported(supported: boolean): void {
    this._hevcSupported = supported
    for (const s of this._sessions) s.setHevcSupported(supported)
  }

  setVp9Supported(supported: boolean): void {
    this._vp9Supported = supported
    for (const s of this._sessions) s.setVp9Supported(supported)
  }

  setAv1Supported(supported: boolean): void {
    this._av1Supported = supported
    for (const s of this._sessions) s.setAv1Supported(supported)
  }

  setInitialNightMode(value: boolean | undefined): void {
    this._initialNightMode = value
    for (const s of this._sessions) s.setInitialNightMode(value)
  }

  // ── Telemetry fan-out: every connected session, like CpManager ──────────────
  sendSpeedData(speedMmS: number, cruiseEngaged?: boolean, cruiseSetSpeedMmS?: number): void {
    for (const s of this._sessions) s.sendSpeedData(speedMmS, cruiseEngaged, cruiseSetSpeedMmS)
  }
  sendRpmData(rpmE3: number): void {
    for (const s of this._sessions) s.sendRpmData(rpmE3)
  }
  sendGearData(gear: number): void {
    for (const s of this._sessions) s.sendGearData(gear)
  }
  sendNightModeData(nightMode: boolean): void {
    for (const s of this._sessions) s.sendNightModeData(nightMode)
  }
  sendParkingBrakeData(engaged: boolean): void {
    for (const s of this._sessions) s.sendParkingBrakeData(engaged)
  }
  sendDrivingStatusData(status: number): void {
    for (const s of this._sessions) s.sendDrivingStatusData(status)
  }
  sendLightData(headLight?: 1 | 2 | 3, hazardLights?: boolean, turnIndicator?: 1 | 2 | 3): void {
    for (const s of this._sessions) s.sendLightData(headLight, hazardLights, turnIndicator)
  }
  sendFuelData(level: number, range?: number, lowFuelWarning?: boolean): void {
    for (const s of this._sessions) s.sendFuelData(level, range, lowFuelWarning)
  }
  sendOdometerData(totalKmE1: number, tripKmE1?: number): void {
    for (const s of this._sessions) s.sendOdometerData(totalKmE1, tripKmE1)
  }
  sendEnvironmentData(temperatureE3?: number, pressureE3?: number, rain?: number): void {
    for (const s of this._sessions) s.sendEnvironmentData(temperatureE3, pressureE3, rain)
  }
  sendGpsLocationData(opts: {
    latDeg: number
    lngDeg: number
    accuracyM?: number
    altitudeM?: number
    speedMs?: number
    bearingDeg?: number
  }): void {
    for (const s of this._sessions) s.sendGpsLocationData(opts)
  }
  sendVehicleEnergyModel(
    capacityWh: number,
    currentWh: number,
    rangeM: number,
    opts?: { maxChargePowerW?: number; maxDischargePowerW?: number; auxiliaryWhPerKm?: number }
  ): void {
    for (const s of this._sessions) s.sendVehicleEnergyModel(capacityWh, currentWh, rangeM, opts)
  }

  setClusterStreamActive(active: boolean): void {
    this._clusterStreamActive = active
    for (const s of this._sessions) s.setClusterStreamActive(active)
  }

  private _seed(): AaSessionSeed {
    return {
      hevcSupported: this._hevcSupported,
      vp9Supported: this._vp9Supported,
      av1Supported: this._av1Supported,
      initialNightMode: this._initialNightMode,
      clusterStreamActive: this._clusterStreamActive
    }
  }

  // ── Wireless sessions from the helper ──────────────────────────────────────

  startWireless(helper: HelperSessionSource | undefined): void {
    if (this._helper) return
    if (!helper) {
      console.warn('[AaManager] wireless AA needs the helper, none is running')
      return
    }
    this._helper = helper
    this._openHelperSub()
    console.log('[AaManager] wireless AA sessions come from the helper')
  }

  private _openHelperSub(): void {
    const helper = this._helper
    if (!helper) return
    this._helperSub = helper.subscribe(
      (ev) => {
        if (ev.event !== 'aa-session' || typeof ev.socket !== 'string') return
        const socket = ev.socket
        const peer = typeof ev.peer === 'string' ? ev.peer : ''
        console.log(`[AaManager] helper session ${socket} from ${peer}`)
        HelperSessionLink.connect(socket, peer)
          .then((link) => {
            if (!this._helper) {
              link.destroy()
              return
            }
            this._supersedeWireless(peer)
            this._spawn(link, false, null, undefined, peer)
          })
          .catch((err: Error) => {
            console.warn(`[AaManager] helper session ${socket}: ${err.message}`)
          })
      },
      () => {
        this._helperSub = null
        if (this._helper) setTimeout(() => this._openHelperSub(), HELPER_RESUBSCRIBE_MS)
      }
    )
  }

  private _closeHelperSub(): void {
    this._helper = null
    const sub = this._helperSub
    this._helperSub = null
    try {
      sub?.close()
    } catch {
      /* already closed */
    }
  }

  stopWireless(): void {
    this._closeHelperSub()
    for (const s of [...this._sessions]) {
      if (!s.isWiredMode()) void s.close()
    }
  }

  async close(): Promise<void> {
    this._closeHelperSub()
    const sessions = [...this._sessions]
    this._sessions.clear()
    await Promise.all(
      sessions.map((s) =>
        s
          .close()
          .catch((e) =>
            console.warn(`[AaManager] session close threw on close: ${(e as Error).message}`)
          )
      )
    )
    const bridges = [...this._wiredBridges.values()]
    this._wiredBridges.clear()
    await Promise.all(
      bridges.map((b) =>
        b
          .stop()
          .catch((e) =>
            console.warn(`[AaManager] wired bridge stop threw on close: ${(e as Error).message}`)
          )
      )
    )
  }

  // ── Wired AOAP bring-up ────────────────────────────────────────────────────

  async bringUpWired(device: Device): Promise<boolean> {
    const key = deviceKey(device)
    if (this._wiredBridges.has(key)) {
      console.log('[AaManager] bringUpWired: bridge already in-flight/up for device, skipping')
      return true
    }
    console.log('[AaManager] _startWiredBridge: bringing up wired AOAP bridge')

    const bridge = new UsbAoapBridge(device, this._onWillReenumerate)
    this._wiredBridges.set(key, bridge)

    bridge.on('error', (err: Error) => {
      console.warn(`[AaManager] wired bridge error: ${err.message}`)
    })
    bridge.on('closed', () => {
      console.log('[AaManager] wired bridge closed')
      if (this._wiredBridges.get(key) === bridge) this._wiredBridges.delete(key)
    })
    bridge.once('ready', ({ host, port }: { host: string; port: number }) => {
      if (this._wiredBridges.get(key) !== bridge) return
      console.log(`[AaManager] wired bridge ready on ${host}:${port}, dialling loopback`)
      const sock = net.createConnection({ host, port, allowHalfOpen: true })
      sock.once('connect', () => {
        if (this._wiredBridges.get(key) !== bridge) {
          try {
            sock.destroy()
          } catch {
            /* ignore */
          }
          return
        }
        console.log('[AaManager] wired loopback connected → spawning AaSession')
        this._spawn(sock, true, bridge, key, undefined, device.serialNumber ?? undefined)
      })
      sock.on('error', (err: Error) => {
        console.warn(`[AaManager] wired loopback socket error: ${err.message}`)
      })
    })

    try {
      await bridge.start(AOAP_LOOPBACK_PORT)
      console.log('[AaManager] wired AA bridge started on loopback')
      return true
    } catch (err) {
      console.error(`[AaManager] wired bridge start failed: ${(err as Error).message}`)
      try {
        await bridge.stop()
      } catch {
        /* ignore */
      }
      this._wiredBridges.delete(key)
      return false
    }
  }

  private _spawn(
    transport: net.Socket | HelperSessionLink,
    wired: boolean,
    wiredBridge: UsbAoapBridge | null,
    key?: string,
    wirelessIp?: string,
    usbSerial?: string
  ): void {
    const session = new AaSession({
      transport,
      getConfig: this._getConfig,
      wired,
      wiredBridge,
      usbSerial,
      seed: this._seed(),
      mediaSink: this._mediaSink
    })
    this._sessions.add(session)
    if (wirelessIp) this._wirelessPeer.set(session, wirelessIp)
    session.once('disconnected', () => {
      this._sessions.delete(session)
      this._wirelessPeer.delete(session)
      if (wired && key && this._wiredBridges.get(key) === wiredBridge) {
        this._wiredBridges.delete(key)
      }
    })
    this._onSpawn(session)
  }

  // Close any existing wireless session from the same phone-IP.
  private _supersedeWireless(ip: string): void {
    if (!ip) return
    for (const [session, peer] of this._wirelessPeer) {
      if (peer === ip) {
        console.log(`[AaManager] wireless reconnect from ${ip}, dropping the superseded session`)
        void session.close()
      }
    }
  }
}

export default AaManager
