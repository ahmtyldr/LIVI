// The ordered, time-boxed service shutdown. Electron runs it from
// app 'before-quit' (lifecycle.ts); the headless entry runs it on SIGTERM.
import { stopSystemVolumeMonitor } from '@main/services/audio/SystemVolume'
import { stopPhoneSuppression } from '@main/services/gvfsPhoneGuard'
import type { ServicesProps } from '@main/types'

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms))

async function withTimeout<T>(label: string, p: Promise<T>, ms: number): Promise<T | undefined> {
  let t: NodeJS.Timeout | undefined
  try {
    return (await Promise.race([
      p,
      new Promise<T | undefined>((resolve) => {
        t = setTimeout(() => {
          console.warn(`[MAIN] before-quit timeout: ${label} after ${ms}ms`)
          resolve(undefined)
        }, ms)
      })
    ])) as T | undefined
  } finally {
    clearTimeout(t)
  }
}

async function measureStep(label: string, fn: () => Promise<unknown>): Promise<void> {
  const t0 = Date.now()
  console.log(`[MAIN] before-quit step:start ${label}`)
  try {
    await fn()
  } finally {
    console.log(`[MAIN] before-quit step:done ${label} (${Date.now() - t0}ms)`)
  }
}

/** Stops sessions, USB, Bluetooth, the helper and the video path, in the
 *  order that has proven safe. Never throws; each step has its own budget. */
export async function shutdownServices(
  services: ServicesProps,
  opts: { beforeSteps?: () => void } = {}
): Promise<void> {
  const { projectionService, usbService, telemetrySocket } = services

  // Safeguards based on measured timings
  const tUsbStop = 500
  const tDisconnect = 800
  const tCarplayStop = 6000
  const tWirelessShutdown = 8000

  try {
    opts.beforeSteps?.()
    projectionService.beginShutdown()
    // Block hotplug callbacks ASAP
    usbService?.beginShutdown()
    stopPhoneSuppression()
    stopSystemVolumeMonitor()

    await measureStep('projection.shutdownWirelessSessions()', async () => {
      await withTimeout(
        'projection.shutdownWirelessSessions()',
        projectionService.shutdownWirelessSessions(),
        tWirelessShutdown
      )
    })
    await measureStep('usbService.stop()', async () => {
      await withTimeout('usbService.stop()', usbService?.stop?.() ?? Promise.resolve(), tUsbStop)
    })
    await measureStep('projection.disconnectPhone()', async () => {
      await withTimeout(
        'projection.disconnectPhone()',
        projectionService.disconnectPhone(),
        tDisconnect
      )
      await sleep(75)
    })
    await measureStep('projection.disconnectHostBtPhones()', async () => {
      await withTimeout(
        'projection.disconnectHostBtPhones()',
        projectionService.disconnectHostBtPhones(),
        1500
      )
    })
    await measureStep('telemetrySocket.disconnect()', async () => {
      await withTimeout(
        'telemetrySocket.disconnect()',
        telemetrySocket?.disconnect?.() ?? Promise.resolve(),
        300
      )
    })
    await measureStep('projection.stopHelper()', async () => {
      await withTimeout('projection.stopHelper()', projectionService.stopHelper(), 2500)
    })
    await measureStep('projection.stop()', async () => {
      await withTimeout('projection.stop()', projectionService.stop(), tCarplayStop)
    })
  } catch (err) {
    console.warn('[MAIN] Error while quitting:', err)
  }
}
