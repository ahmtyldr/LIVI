// Supervises native/livi-ui, the LVGL front end used when LIVI_UI=lvgl.
// The binary is a Wayland client of the nested compositor this process runs
// under, so it inherits WAYLAND_DISPLAY and XDG_RUNTIME_DIR from here.
import { type ChildProcess, spawn } from 'node:child_process'
import { existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { appRoot } from '@main/host/paths'
import { uiBridgeSocketPath } from '@main/ui-bridge'

const TAG = '[livi-ui]'

/** LIVI_UI_BIN, else resources/ui/livi-ui (packaged), else out/ui/livi-ui (dev). */
export function uiBinaryPath(): string | undefined {
  const override = process.env.LIVI_UI_BIN
  if (override) return override
  const candidates = [
    process.resourcesPath ? join(process.resourcesPath, 'ui', 'livi-ui') : undefined,
    join(appRoot(), 'out', 'ui', 'livi-ui')
  ]
  return candidates.find((p): p is string => !!p && existsSync(p))
}

export type UiProcessHandle = {
  /** Stops respawning and terminates the running instance. */
  stop: () => Promise<void>
  pid: () => number | undefined
}

/** Starts livi-ui and restarts it when it exits (1 s → 10 s backoff).
 *  Returns undefined when no binary is available; main keeps running so the
 *  socket clients (tools/ui-cli.mjs) still work. */
export function startUiProcess(): UiProcessHandle | undefined {
  const bin = uiBinaryPath()
  if (!bin) {
    console.warn(`${TAG} no binary (LIVI_UI_BIN or resources/ui/livi-ui); running without a UI`)
    return undefined
  }
  const resources = process.env.LIVI_UI_RESOURCES ?? dirname(bin)
  const socket = uiBridgeSocketPath()
  // tooling hook (tools/parity): "page /media" switches pages
  const ctl = process.env.LIVI_UI_CTL ?? (socket ? join(dirname(socket), 'livi-ui.ctl') : undefined)
  let child: ChildProcess | undefined
  let stopped = false
  let backoff = 1000
  let timer: NodeJS.Timeout | undefined

  const launch = (): void => {
    if (stopped) return
    const startedAt = Date.now()
    child = spawn(bin, [], {
      env: {
        ...process.env,
        LIVI_UI_RESOURCES: resources,
        ...(socket ? { LIVI_UI_SOCKET: socket } : {}),
        ...(ctl ? { LIVI_UI_CTL: ctl } : {})
      },
      stdio: ['ignore', 'pipe', 'pipe']
    })
    console.log(`${TAG} started pid ${child.pid} (${bin})`)
    const relay = (line: string): void => {
      const text = line.trimEnd()
      if (text) console.log(text.startsWith(TAG) ? text : `${TAG} ${text}`)
    }
    child.stdout?.on('data', (d: Buffer) => d.toString().split('\n').forEach(relay))
    child.stderr?.on('data', (d: Buffer) => d.toString().split('\n').forEach(relay))
    child.on('error', (e) => console.warn(`${TAG} spawn failed: ${e.message}`))
    child.on('exit', (code, signal) => {
      child = undefined
      if (stopped) return
      const ran = Date.now() - startedAt
      backoff = ran > 30000 ? 1000 : Math.min(backoff * 2, 10000)
      console.warn(`${TAG} exited (code ${code}, signal ${signal}) after ${ran} ms; restart in ${backoff} ms`)
      timer = setTimeout(launch, backoff)
    })
  }
  launch()

  return {
    pid: () => child?.pid,
    stop: () =>
      new Promise<void>((resolve) => {
        stopped = true
        if (timer) clearTimeout(timer)
        const c = child
        if (!c) return resolve()
        const kill = setTimeout(() => c.kill('SIGKILL'), 2000)
        c.once('exit', () => {
          clearTimeout(kill)
          resolve()
        })
        c.kill('SIGTERM')
      })
  }
}
