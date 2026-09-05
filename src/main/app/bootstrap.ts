// The part of start-up that does not care who draws the UI. index.ts (Electron)
// and headless.ts (LIVI_UI=lvgl, plain Node) both run this; window creation,
// the custom protocol, install dialogs and the Electron lifecycle stay in
// index.ts, the bridge renderer and signal handling in headless.ts.
import { setDebugLogging } from '@main/constants'
import { configEvents, saveSettings } from '@main/ipc/utils'
import { seedCustomPage, setCustomPageConfig } from '@main/protocol/appProtocol'
import {
  setSystemVolume,
  startSystemVolumeMonitor,
  stopSystemVolumeMonitor
} from '@main/services/audio/SystemVolume'
import { CarBridgeService } from '@main/services/carBridge/CarBridgeService'
import { customProxy } from '@main/services/custom/CustomProxy'
import { ProjectionService } from '@main/services/projection/services/ProjectionService'
import { TelemetrySocket } from '@main/services/Socket'
import { TelemetryStore } from '@main/services/telemetry/TelemetryStore'
import { USBService } from '@main/services/usb/USBService'
import { setCompositorBackdrop, setStreamGamma } from '@main/services/video/GstVideo'
import type { runtimeStateProps, ServicesProps } from '@main/types'
import type { Config } from '@shared/types'
import { loadConfig } from '../config/loadConfig'

export type Core = {
  runtimeState: runtimeStateProps
  services: ServicesProps
  projectionService: ProjectionService
  usbService: USBService
  telemetryStore: TelemetryStore
  telemetrySocket: TelemetrySocket
  carBridge: CarBridgeService
}

/** Services, config and the config-driven wiring between them. */
export async function createCore(): Promise<Core> {
  const projectionService = new ProjectionService()
  const usbService = new USBService(projectionService)
  const telemetryStore = new TelemetryStore()
  const telemetrySocket = new TelemetrySocket(telemetryStore, 4000)

  const runtimeState: runtimeStateProps = {
    config: loadConfig(),
    telemetrySocket: null,
    isQuitting: false,
    suppressNextFsSync: false,
    wmExitedKiosk: false
  }
  setDebugLogging(runtimeState.config.debugLogging === true)

  setCustomPageConfig(() => runtimeState.config)
  seedCustomPage()
  await customProxy.start(runtimeState.config.customUrl)
  configEvents.on('changed', (next: Config) => {
    void customProxy.start(next.customUrl)
  })

  const carBridge = new CarBridgeService(runtimeState.config.language)
  carBridge.start()
  projectionService.onProjectionEvent((payload) => carBridge.handleEvent(payload))
  carBridge.onKey = (command) => projectionService.dispatchRemoteInput(command)
  carBridge.onTelemetry = (payload) => telemetryStore.merge(payload)
  carBridge.setBrightness(runtimeState.config.displayBrightness * 100)
  configEvents.on('changed', (next: Config) =>
    carBridge.setBrightness(next.displayBrightness * 100)
  )
  // auto: the vehicle's panel dimmer writes displayBrightness itself, so the
  // slider stays truthful; manual: vehicle values run into the void
  let brightnessAuto = runtimeState.config.displayBrightnessAuto
  configEvents.on('changed', (next: Config) => {
    brightnessAuto = next.displayBrightnessAuto
  })
  telemetryStore.on('change', (patch: { dimmerPct?: unknown }) => {
    if (!brightnessAuto || typeof patch.dimmerPct !== 'number') return
    const next = Math.min(1, Math.max(0, patch.dimmerPct / 100))
    if (Math.abs(next - runtimeState.config.displayBrightness) < 0.005) return
    saveSettings(runtimeState, { displayBrightness: next })
  })

  runtimeState.telemetrySocket = telemetrySocket
  const services: ServicesProps = { projectionService, usbService, telemetrySocket }
  return {
    runtimeState,
    services,
    projectionService,
    usbService,
    telemetryStore,
    telemetrySocket,
    carBridge
  }
}

/** Backdrop colour, stream calibration and head-unit volume, applied now and
 *  on every config change. `onBackdrop` lets Electron paint its windows too. */
export function applyConfigBehaviours(
  core: Core,
  opts: { onBackdrop?: (hex: string) => void } = {}
): void {
  const { runtimeState } = core

  const applyBackdrop = (cfg: Config): void => {
    const color = backdropHexOf(cfg)
    setCompositorBackdrop(color)
    opts.onBackdrop?.(color)
  }
  applyBackdrop(runtimeState.config)
  configEvents.on('changed', (next: Config) => applyBackdrop(next))

  const applyGamma = (cfg: Config): void => {
    setStreamGamma(
      cfg.displayGamma,
      cfg.displayContrast,
      cfg.displayColorR,
      cfg.displayColorG,
      cfg.displayColorB
    )
  }
  applyGamma(runtimeState.config)
  configEvents.on('changed', (next: Config) => applyGamma(next))

  let appliedHuVolume: number | null = null
  const applyHuVolume = (cfg: Config): void => {
    if (cfg.huVolumeLinkSystem !== true) {
      appliedHuVolume = null
      stopSystemVolumeMonitor()
      return
    }
    startSystemVolumeMonitor(
      () => runtimeState.config.audioOutputDevice,
      (level) => {
        if (runtimeState.config.huVolumeLinkSystem !== true) return
        if (Math.abs(level - runtimeState.config.huVolume) < 0.005) return
        appliedHuVolume = level
        console.log(`[SystemVolume] head unit follows system → ${Math.round(level * 100)} %`)
        saveSettings(runtimeState, { huVolume: level })
      }
    )
    if (appliedHuVolume !== null && Math.abs(cfg.huVolume - appliedHuVolume) < 0.005) return
    appliedHuVolume = cfg.huVolume
    void setSystemVolume(cfg.huVolume, cfg.audioOutputDevice)
  }
  applyHuVolume(runtimeState.config)
  configEvents.on('changed', (next: Config) => applyHuVolume(next))
}

/** Last step: hand the config to projection and start a session if one is due. */
export async function finishStart(core: Core): Promise<void> {
  core.projectionService.applyConfigPatch(core.runtimeState.config)
  await core.projectionService.autoStartIfNeeded()
}

import { backdropHex } from '@main/services/video/GstVideo'

function backdropHexOf(cfg: Config): string {
  return backdropHex(cfg.darkMode, cfg.backgroundColorDark, cfg.backgroundColorLight)
}
