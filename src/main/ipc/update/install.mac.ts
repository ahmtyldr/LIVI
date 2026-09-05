import { getUiHost } from '@main/host/ui'
import { installFromDmg } from '@main/ipc/update/install.dmg'
import { sendUpdateEvent } from '@main/ipc/utils'

export async function installOnMacFromFile(dmgPath: string): Promise<void> {
  if (process.platform !== 'darwin') throw new Error('macOS only')
  await installFromDmg(dmgPath)
  sendUpdateEvent({ phase: 'relaunching' })
  getUiHost().relaunch()
  setImmediate(() => getUiHost().quit())
}
