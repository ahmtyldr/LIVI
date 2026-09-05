#!/usr/bin/env node
// Command-line client for the LIVI UI bridge (JSON-RPC 2.0 over a Unix socket).
//
//   ui-cli rpc.describe                       list methods and event channels
//   ui-cli projection.settings.get            call a method (params as JSON)
//   ui-cli projection.ipc.sendTouch 100 200 0
//   ui-cli projection.settings.save '{"darkMode":true}'
//   ui-cli --watch [channel]                  print events as they arrive
//   ui-cli --socket /path/livi-ui.sock ...    override the socket path
//
// Runs on plain Node or, on a kiosk without Node, through the AppImage:
//   ELECTRON_RUN_AS_NODE=1 ~/LIVI/LIVI.AppImage tools/ui-cli.mjs rpc.ping
import { connect } from 'node:net'
import { join } from 'node:path'

const argv = process.argv.slice(2)
let socket = process.env.LIVI_UI_SOCKET
let watch = false
let watchChannel = ''
let timeoutMs = 10000
const rest = []
for (let i = 0; i < argv.length; i++) {
  const a = argv[i]
  if (a === '--socket') socket = argv[++i]
  else if (a === '--watch') {
    watch = true
    if (argv[i + 1] && !argv[i + 1].startsWith('--')) watchChannel = argv[++i]
  } else if (a === '--timeout') timeoutMs = Number(argv[++i])
  else if (a === '--help' || a === '-h') {
    console.log('usage: ui-cli [--socket PATH] (<method> [params...] | --watch [channel])')
    process.exit(0)
  } else rest.push(a)
}
if (!socket) {
  const dir = process.env.XDG_RUNTIME_DIR
  socket = dir ? join(dir, 'livi-ui.sock') : `/tmp/livi-ui-${process.getuid?.() ?? 0}.sock`
}

const parseParam = (s) => {
  try {
    return JSON.parse(s)
  } catch {
    return s
  }
}

const conn = connect(socket)
let buffer = ''
let nextId = 1
const pending = new Map()

conn.on('error', (e) => {
  console.error(`ui-cli: cannot connect to ${socket}: ${e.message}`)
  process.exit(2)
})

conn.on('data', (chunk) => {
  buffer += chunk.toString('utf8')
  const lines = buffer.split('\n')
  buffer = lines.pop() ?? ''
  for (const line of lines) {
    if (!line.trim()) continue
    let msg
    try {
      msg = JSON.parse(line)
    } catch {
      console.error('ui-cli: bad frame', line.slice(0, 120))
      continue
    }
    if (msg.method === 'event') {
      if (watch && (!watchChannel || msg.params.channel === watchChannel)) {
        const args = msg.params.args.map(shorten)
        console.log(`${new Date().toISOString().slice(11, 23)} ${msg.params.channel} ${JSON.stringify(args)}`)
      }
      continue
    }
    const p = pending.get(msg.id)
    if (!p) continue
    pending.delete(msg.id)
    if (msg.error) p.reject(msg.error)
    else p.resolve(msg.result)
  }
})

function shorten(v) {
  if (v && typeof v === 'object' && typeof v.$bytes === 'string') return `<${Math.round((v.$bytes.length * 3) / 4)} bytes>`
  return v
}

function call(method, params) {
  return new Promise((resolve, reject) => {
    const id = nextId++
    pending.set(id, { resolve, reject })
    conn.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`)
    setTimeout(() => {
      if (pending.delete(id)) reject(new Error(`timeout after ${timeoutMs} ms`))
    }, timeoutMs).unref()
  })
}

conn.on('connect', async () => {
  if (watch) {
    console.error(`ui-cli: watching ${watchChannel || 'all events'} on ${socket} (Ctrl+C to stop)`)
    return
  }
  const [method, ...params] = rest
  if (!method) {
    console.error('ui-cli: method required (try rpc.describe)')
    process.exit(1)
  }
  try {
    const result = await call(method, params.map(parseParam))
    console.log(JSON.stringify(result, null, 2))
    conn.end()
  } catch (e) {
    console.error(`ui-cli: ${method} failed: ${e.message ?? JSON.stringify(e)}`)
    conn.end()
    process.exit(1)
  }
})
