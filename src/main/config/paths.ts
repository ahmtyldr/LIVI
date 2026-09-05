import { userDataDir } from '@main/host/paths'
import { join } from 'path'

export const CONFIG_PATH = join(userDataDir(), 'config.json')
