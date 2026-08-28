import { spawn } from 'node:child_process'
import { existsSync, readFileSync } from 'node:fs'
import os from 'node:os'
import { join } from 'node:path'
import { app, BrowserWindow, dialog } from 'electron'

const UNIT_PATH = '/etc/systemd/system/livi-wifi-ap.service'

// Early-boot AP ownership: the staged helper runs hostapd/dnsmasq before LIVI starts,
// so a phone can associate the moment the device is powered.
function unitContent(): string {
  const helper = join(app.getPath('userData'), 'driver', 'livi-helperd')
  const user = os.userInfo().username
  return `[Unit]
Description=LIVI wireless projection AP (early boot)
After=network-pre.target
Wants=network-pre.target
ConditionPathExists=${helper}

[Service]
Type=simple
Environment=SUDO_USER=${user}
ExecStart=${helper} --wifi-ap
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
`
}

function installedContent(): string {
  try {
    return existsSync(UNIT_PATH) ? readFileSync(UNIT_PATH, 'utf8') : ''
  } catch {
    return ''
  }
}

function installUnit(content: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const script = [
      'set -e',
      `cat > ${UNIT_PATH} <<'EOF'`,
      content.trimEnd(),
      'EOF',
      'systemctl daemon-reload',
      'systemctl enable livi-wifi-ap.service',
      'systemctl restart livi-wifi-ap.service'
    ].join('\n')
    const proc = spawn('pkexec', ['bash', '-c', script], { stdio: 'ignore' })
    proc.on('close', (code) =>
      code === 0 ? resolve() : reject(new Error(`pkexec exited with code ${code}`))
    )
    proc.on('error', reject)
  })
}

export async function checkAndInstallWifiApUnit(window: BrowserWindow): Promise<void> {
  if (process.platform !== 'linux') return
  const wanted = unitContent()
  if (installedContent() === wanted) return

  const { response } = await dialog.showMessageBox(window, {
    type: 'question',
    title: 'Wireless Projection — Wi-Fi AP Service',
    message: 'LIVI needs a boot service so the projection Wi-Fi AP starts with the device.',
    detail: `A systemd unit will be installed at ${UNIT_PATH} running the LIVI helper in AP mode.`,
    buttons: ['Install', 'Skip'],
    defaultId: 0,
    cancelId: 1
  })
  if (response !== 0) return

  try {
    await installUnit(wanted)
    console.log('[wifiApUnit] installed and started livi-wifi-ap.service')
  } catch (err) {
    console.error('[wifiApUnit] installation failed:', err)
  }
}
