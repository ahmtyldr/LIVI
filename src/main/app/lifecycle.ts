import { shutdownServices } from '@main/app/shutdown'
import { runPendingPowerAction } from '@main/services/power/hostPower'
import { runtimeStateProps, ServicesProps } from '@main/types'
import { createMainWindow, getMainWindow } from '@main/window/createWindow'
import { closeAllSecondaryWindows } from '@main/window/secondaryWindows'
import { app, BrowserWindow } from 'electron'

export function setupLifecycle(runtimeState: runtimeStateProps, services: ServicesProps) {
  const mainWindow = getMainWindow()

  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit()
  })

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0 && !mainWindow)
      createMainWindow(runtimeState, services)
    else mainWindow?.show()
  })

  app.on('before-quit', async (e) => {
    if (runtimeState.isQuitting) return
    runtimeState.isQuitting = true
    e.preventDefault()

    const watchdogMs = process.platform === 'darwin' ? 10000 : 3000
    const watchdog = setTimeout(() => {
      console.warn(`[MAIN] before-quit watchdog: giving up waiting after ${watchdogMs}ms`)
    }, watchdogMs)

    try {
      await shutdownServices(services, { beforeSteps: () => closeAllSecondaryWindows() })
    } catch (err) {
      console.warn('[MAIN] Error while quitting:', err)
    } finally {
      setTimeout(() => clearTimeout(watchdog), 250)

      runPendingPowerAction()
      setImmediate(() => process.kill(process.pid, 'SIGKILL'))
    }
  })

  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit()
  })
}
