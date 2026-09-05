import './logTimestamps'
import './app/gpu'
import { applyConfigBehaviours, createCore, finishStart } from '@main/app/bootstrap'
import { bootstrapCompositor } from '@main/app/compositorBootstrap'
import { installMainProcessErrorHandlers } from '@main/app/errorHandler'
import { setupAppIdentity } from '@main/app/init'
import { setupLifecycle } from '@main/app/lifecycle'

installMainProcessErrorHandlers()

import { registerIpc } from '@main/ipc'
import { saveSettings } from '@main/ipc/utils'
import { registerAppProtocol } from '@main/protocol/appProtocol'
import { ensureWireplumberBtRoles } from '@main/services/audio/wireplumberBtRoles'
import { checkAndInstallGvfsGuard, startPhoneSuppression } from '@main/services/gvfsPhoneGuard'
import { checkMissingPackages } from '@main/services/packageCheck'
import { checkAndInstallHelperSudoers } from '@main/services/projection/driver/helper/helperSudoers'
import { checkAndInstallWifiApUnit } from '@main/services/projection/driver/helper/wifiApUnit'
import { setupTelemetry } from '@main/services/telemetry/setupTelemetry'
import { installRendererSendTap, startUiBridge, stopUiBridge } from '@main/ui-bridge'
import { app, BrowserWindow } from 'electron'
import { restartApp } from './ipc/app'
import { checkAndInstallUdevRule } from './services/usb/udevRule'
import { setMacBackdrop } from './services/video/GstVideo'
import { createMainWindow, getMainWindow } from './window/createWindow'
import { setupSecondaryWindows } from './window/secondaryWindows'

// Outer launcher hands off to the nested compositor and exits
let bootAllowed = true
if (bootstrapCompositor()) {
  app.exit(0)
  bootAllowed = false
} else if (!app.requestSingleInstanceLock()) {
  // Another LIVI already owns the telemetry port and the USB device, do not start a rival
  app.exit(0)
  bootAllowed = false
} else {
  app.on('second-instance', () => {
    const win = getMainWindow()
    if (!win) return
    if (win.isMinimized()) win.restore()
    win.show()
    win.focus()
  })
}

app.whenReady().then(async () => {
  if (!bootAllowed) return
  const core = await createCore()
  const { runtimeState, services, projectionService, telemetryStore } = core

  setupAppIdentity()
  registerAppProtocol()
  registerIpc(runtimeState, services)
  createMainWindow(runtimeState, services)
  setupSecondaryWindows(runtimeState)

  // JSON-RPC bridge for a second UI (native/livi-ui); mirrors renderer events.
  startUiBridge()
    .then(() => installRendererSendTap(getMainWindow()?.webContents))
    .catch((e) => console.warn('[ui-bridge] not started:', (e as Error).message))
  app.on('will-quit', () => stopUiBridge())

  // Linux: the compositor draws the backdrop. macOS: paint the window content view itself.
  applyConfigBehaviours(core, {
    onBackdrop: (color) => {
      for (const w of BrowserWindow.getAllWindows()) setMacBackdrop(w, color)
    }
  })
  setupTelemetry({
    store: telemetryStore,
    projectionService,
    initialConfig: runtimeState.config
  })
  setupLifecycle(runtimeState, services)

  const win = getMainWindow()
  if (win && (await checkAndInstallUdevRule(win))) {
    await restartApp(runtimeState, services)
    return
  }

  if (
    win &&
    process.platform === 'linux' &&
    (runtimeState.config.wirelessAaEnabled === true ||
      runtimeState.config.wirelessCpEnabled === true)
  ) {
    await checkAndInstallHelperSudoers(win)
    await checkAndInstallWifiApUnit(win)
  }

  if (win && process.platform === 'linux') {
    await checkAndInstallGvfsGuard(win)
    startPhoneSuppression()
    ensureWireplumberBtRoles()
  }

  if (win && process.platform === 'linux') {
    const { dismissed } = await checkMissingPackages(win, runtimeState.config.dismissedPackages)
    if (dismissed) saveSettings(runtimeState, { dismissedPackages: dismissed })
  }

  await finishStart(core)
})
