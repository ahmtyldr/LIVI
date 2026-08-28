import { spawn } from 'node:child_process'
import { EventEmitter } from 'node:events'
import { existsSync, readFileSync } from 'node:fs'
import { dialog } from 'electron'
import { afterEach, beforeEach, describe, expect, type Mock, test, vi } from 'vitest'

vi.mock('node:child_process', () => ({ spawn: vi.fn() }))
vi.mock('node:fs', () => ({ existsSync: vi.fn(), readFileSync: vi.fn() }))
vi.mock('node:os', () => ({
  default: { userInfo: () => ({ username: 'pi' }) },
  userInfo: () => ({ username: 'pi' })
}))
vi.mock('electron', () => ({
  app: { getPath: vi.fn(() => '/data') },
  dialog: { showMessageBox: vi.fn(() => Promise.resolve({ response: 0 })) }
}))

import { checkAndInstallWifiApUnit } from '../wifiApUnit'

const mockedSpawn = spawn as Mock
const mockedExists = existsSync as Mock
const mockedRead = readFileSync as Mock
const mockedDialog = dialog.showMessageBox as Mock
const win = {} as never

function makeProc(): EventEmitter {
  return new EventEmitter()
}

describe('checkAndInstallWifiApUnit', () => {
  let realPlatform: PropertyDescriptor | undefined
  beforeEach(() => {
    realPlatform = Object.getOwnPropertyDescriptor(process, 'platform')
    Object.defineProperty(process, 'platform', { value: 'linux', configurable: true })
    vi.clearAllMocks()
  })
  afterEach(() => {
    if (realPlatform) Object.defineProperty(process, 'platform', realPlatform)
  })

  test('installs the unit via pkexec when it is missing', async () => {
    mockedExists.mockReturnValue(false)
    const proc = makeProc()
    mockedSpawn.mockReturnValue(proc)
    const p = checkAndInstallWifiApUnit(win)
    await Promise.resolve()
    proc.emit('close', 0)
    await p
    expect(mockedDialog).toHaveBeenCalled()
    const [cmd, args] = mockedSpawn.mock.calls[0]
    expect(cmd).toBe('pkexec')
    expect(String(args[2])).toContain('/data/driver/livi-helperd --wifi-ap')
    expect(String(args[2])).toContain('systemctl enable livi-wifi-ap.service')
  })

  test('does nothing when the installed unit already matches', async () => {
    mockedExists.mockReturnValue(true)
    mockedRead.mockImplementation(() =>
      [
        '[Unit]',
        'Description=LIVI wireless projection AP (early boot)',
        'After=network-pre.target',
        'Wants=network-pre.target',
        'ConditionPathExists=/data/driver/livi-helperd',
        '',
        '[Service]',
        'Type=simple',
        'Environment=SUDO_USER=pi',
        'ExecStart=/data/driver/livi-helperd --wifi-ap',
        'Restart=on-failure',
        'RestartSec=5',
        '',
        '[Install]',
        'WantedBy=multi-user.target',
        ''
      ].join('\n')
    )
    await checkAndInstallWifiApUnit(win)
    expect(mockedDialog).not.toHaveBeenCalled()
    expect(mockedSpawn).not.toHaveBeenCalled()
  })

  test('skips installation when the user declines', async () => {
    mockedExists.mockReturnValue(false)
    mockedDialog.mockResolvedValueOnce({ response: 1 })
    await checkAndInstallWifiApUnit(win)
    expect(mockedSpawn).not.toHaveBeenCalled()
  })

  test('does not throw when pkexec exits non-zero', async () => {
    mockedExists.mockReturnValue(false)
    const proc = makeProc()
    mockedSpawn.mockReturnValue(proc)
    const p = checkAndInstallWifiApUnit(win)
    await Promise.resolve()
    proc.emit('close', 126)
    await expect(p).resolves.toBeUndefined()
  })

  test('treats an unreadable unit file as absent and installs', async () => {
    mockedExists.mockReturnValue(true)
    mockedRead.mockImplementation(() => {
      throw new Error('EACCES')
    })
    const proc = makeProc()
    mockedSpawn.mockReturnValue(proc)
    const p = checkAndInstallWifiApUnit(win)
    await Promise.resolve()
    proc.emit('close', 0)
    await p
    expect(mockedSpawn).toHaveBeenCalled()
  })

  test('is a no-op off linux', async () => {
    Object.defineProperty(process, 'platform', { value: 'darwin', configurable: true })
    await checkAndInstallWifiApUnit(win)
    expect(mockedDialog).not.toHaveBeenCalled()
  })
})
