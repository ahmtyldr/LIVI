// LIVI without Electron: the main process on plain Node, the UI on the other
// end of the JSON-RPC bridge (native/livi-ui). Built by vite.headless.config.mts
// into out/main/headless.js, next to main.js so every relative resource path
// (protos, driver, preload) resolves the same way.
//
// On a kiosk it runs through the AppImage's own Node:
//   ELECTRON_RUN_AS_NODE=1 LIVI_UI=lvgl LIVI.AppImage \
//     -e "require(process.resourcesPath + '/app.asar/out/main/headless.js')"
import './logTimestamps'
import { applyConfigBehaviours, createCore, finishStart } from '@main/app/bootstrap'
import { bootstrapCompositor } from '@main/app/compositorBootstrap'
import { installMainProcessErrorHandlers } from '@main/app/errorHandler'
import { shutdownServices } from '@main/app/shutdown'
import { bridgeRenderer } from '@main/host/renderer'
import { registerIpc } from '@main/ipc'
import { runPendingPowerAction } from '@main/services/power/hostPower'
import { setupTelemetry } from '@main/services/telemetry/setupTelemetry'
import { startUiBridge, stopUiBridge, uiBridgeSocketPath } from '@main/ui-bridge'

installMainProcessErrorHandlers()
process.env.LIVI_UI ??= 'lvgl'

/** The command the nested compositor runs to relaunch this entry inside it. */
function headlessInnerCommand(): string {
  const relaunch = process.env.APPIMAGE ?? process.execPath
  const script = process.env.APPIMAGE
    ? `require(process.resourcesPath + '/app.asar/out/main/headless.js')`
    : `require(${JSON.stringify(__filename)})`
  const hostLd = process.env.LD_LIBRARY_PATH ?? ''
  return (
    `ELECTRON_RUN_AS_NODE=1 LIVI_UI=${process.env.LIVI_UI} LIVI_COMPOSITOR=1 ` +
    `LD_LIBRARY_PATH='${hostLd}' '${relaunch}' -e "${script}"`
  )
}

async function main(): Promise<void> {
  // Outer launcher hands off to the nested compositor and exits, like index.ts.
  if (bootstrapCompositor(headlessInnerCommand())) {
    process.exit(0)
  }

  const t0 = Date.now()
  const core = await createCore()
  const { runtimeState, services, projectionService, telemetryStore } = core

  registerIpc(runtimeState, services)
  await startUiBridge()
  console.log(`[headless] bridge at ${uiBridgeSocketPath()}`)

  // The bridge stands in for the renderer: projection events, cluster
  // resolution and video-plane bookkeeping all flow through it.
  projectionService.attachRenderer(bridgeRenderer())

  applyConfigBehaviours(core)
  setupTelemetry({ store: telemetryStore, projectionService, initialConfig: runtimeState.config })

  let quitting = false
  const quit = async (signal: string): Promise<void> => {
    if (quitting) return
    quitting = true
    runtimeState.isQuitting = true
    console.log(`[headless] ${signal}: shutting down`)
    const watchdog = setTimeout(() => process.exit(1), 12000)
    await shutdownServices(services)
    stopUiBridge()
    clearTimeout(watchdog)
    runPendingPowerAction()
    process.exit(0)
  }
  process.on('SIGTERM', () => void quit('SIGTERM'))
  process.on('SIGINT', () => void quit('SIGINT'))
  process.on('SIGHUP', () => void quit('SIGHUP'))

  await finishStart(core)
  const rss = Math.round(process.memoryUsage().rss / 1048576)
  console.log(`[headless] up in ${Date.now() - t0} ms, rss ${rss} MB, ui=${process.env.LIVI_UI}`)
}

// Importing this module (tests, tooling) must not start anything.
if (!process.env.VITEST) {
  void main().catch((e) => {
    console.error('[headless] fatal:', e)
    process.exit(1)
  })
}

export { main as startHeadless }
