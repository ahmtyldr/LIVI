import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const electronMock = vi.hoisted(() => ({
  app: {
    getPath: vi.fn(() => '/data'),
    getAppPath: vi.fn(() => '/repo'),
    getVersion: vi.fn(() => '1.2.3'),
    isPackaged: false,
    quit: vi.fn(),
    relaunch: vi.fn()
  },
  BrowserWindow: { getAllWindows: vi.fn(() => [] as unknown[]) },
  dialog: { showMessageBox: vi.fn(async () => ({ response: 1 })) },
  shell: { openExternal: vi.fn(async () => {}) }
}))
vi.mock('electron', () => electronMock)
vi.mock('@main/ui-bridge/server', () => ({ bridgeEmit: vi.fn() }))

import { bridgeEmit } from '@main/ui-bridge/server'
import { appRoot, appVersion, isPackaged, userDataDir } from '../paths'
import { electronUiHost, getUiHost, setUiHostForTests, socketUiHost, uiMode } from '../ui'

describe('host/paths', () => {
  it('reads paths from Electron when app is present', () => {
    expect(userDataDir()).toBe('/data')
    expect(electronMock.app.getPath).toHaveBeenCalledWith('userData')
    expect(appRoot()).toBe('/repo')
    expect(appVersion()).toBe('1.2.3')
    expect(isPackaged()).toBe(false)
    electronMock.app.isPackaged = true
    expect(isPackaged()).toBe(true)
    electronMock.app.isPackaged = false
  })

  it('falls back to the environment without Electron', () => {
    const saved = { ...electronMock.app }
    const stub = electronMock.app as unknown as Record<string, unknown>
    stub.getPath = undefined
    stub.getAppPath = undefined
    stub.getVersion = undefined
    delete stub.isPackaged
    process.env.LIVI_USER_DATA = '/srv/livi'
    process.env.LIVI_APP_ROOT = '/opt/livi'
    process.env.LIVI_VERSION = '9.9.9'
    process.env.LIVI_PACKAGED = '1'
    try {
      expect(userDataDir()).toBe('/srv/livi')
      expect(appRoot()).toBe('/opt/livi')
      expect(appVersion()).toBe('9.9.9')
      expect(isPackaged()).toBe(true)
    } finally {
      Object.assign(electronMock.app, saved)
      delete process.env.LIVI_USER_DATA
      delete process.env.LIVI_APP_ROOT
      delete process.env.LIVI_VERSION
      delete process.env.LIVI_PACKAGED
    }
  })
})

describe('host/ui', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })
  afterEach(() => {
    setUiHostForTests(undefined)
    delete process.env.LIVI_UI
  })

  it('selects the host from LIVI_UI', () => {
    expect(uiMode()).toBe('electron')
    expect(getUiHost().kind).toBe('electron')
    process.env.LIVI_UI = 'lvgl'
    expect(uiMode()).toBe('socket')
    expect(getUiHost().kind).toBe('socket')
  })

  it('electron host delegates dialogs, links and quit to Electron', async () => {
    const win = { id: 7 }
    const res = await electronUiHost.showMessageBox({ message: 'hi', buttons: ['a', 'b'] }, win)
    expect(res.response).toBe(1)
    expect(electronMock.dialog.showMessageBox).toHaveBeenCalledWith(win, {
      message: 'hi',
      buttons: ['a', 'b']
    })
    await electronUiHost.openExternal('https://example.org')
    expect(electronMock.shell.openExternal).toHaveBeenCalledWith('https://example.org')
    electronUiHost.quit()
    expect(electronMock.app.quit).toHaveBeenCalled()
    electronUiHost.relaunch()
    expect(electronMock.app.relaunch).toHaveBeenCalled()
  })

  it('electron host broadcasts to every live window', () => {
    const send = vi.fn()
    electronMock.BrowserWindow.getAllWindows.mockReturnValue([
      { isDestroyed: () => false, webContents: { send } },
      { isDestroyed: () => true, webContents: { send } },
      { webContents: { send } }
    ])
    electronUiHost.broadcast('usb-event', { type: 'plugged' })
    expect(send).toHaveBeenCalledTimes(2)
    expect(send).toHaveBeenCalledWith('usb-event', { type: 'plugged' })
  })

  it('socket host routes everything through the bridge and answers dialogs by default', async () => {
    const res = await socketUiHost.showMessageBox({
      message: 'install?',
      buttons: ['Install', 'Skip'],
      defaultId: 1
    })
    expect(res.response).toBe(1)
    expect(bridgeEmit).toHaveBeenCalledWith(
      'ui:dialog',
      expect.objectContaining({ message: 'install?' })
    )
    socketUiHost.broadcast('telemetry:update', { speedKph: 1 })
    expect(bridgeEmit).toHaveBeenCalledWith('telemetry:update', { speedKph: 1 })
  })

  it('test override wins', () => {
    setUiHostForTests(socketUiHost)
    expect(getUiHost().kind).toBe('socket')
  })
})
