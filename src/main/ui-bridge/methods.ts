// The bridge's method table, built from contracts/ui-api.json so the socket
// exposes exactly what the preload exposes to the React renderer. Each call
// maps to the IPC channel its preload wrapper uses; the wrapper's argument
// expression (e.g. `{ x, y, action }`) is compiled into an adapter so the
// bridge sends the handler the same shape the renderer would.
import contract from '../../../contracts/ui-api.json'

export type Transport = 'invoke' | 'send' | 'local'

export type ContractCall = {
  name: string
  params: { name: string; type: string; optional: boolean }[]
  returns: string
  transport: Transport
  channel?: string
  channelArgs?: string
}

export type ContractEvent = { channel: string; via: string }

export type BridgeMethod = {
  name: string
  transport: Transport
  channel?: string
  /** Maps positional RPC params to the arguments the IPC handler expects. */
  args: (params: unknown[]) => unknown[]
  params: ContractCall['params']
  returns: string
}

const calls = contract.calls as ContractCall[]
const events = contract.events as ContractEvent[]

/** Values the preload exposes without any IPC round trip. */
export const LOCAL_VALUES: Record<string, () => unknown> = {
  'app.platform': () => process.platform,
  'app.compositor': () => process.env.LIVI_COMPOSITOR === '1'
}

const IDENT = /^[A-Za-z_$][\w$]*$/

/** Compiles `channelArgs` (the preload's argument expression) into a function
 *  of the call's named parameters. The expression comes from our own bundle,
 *  never from the socket, so evaluating it is safe. */
export function compileArgs(call: ContractCall): BridgeMethod['args'] {
  const names = call.params.map((p) => p.name).filter((n) => IDENT.test(n))
  const expr = (call.channelArgs ?? '').trim()
  if (!expr) return () => []
  if (names.length === call.params.length && expr === names.join(', ')) {
    return (params) => params.slice(0, names.length)
  }
  try {
    const fn = new Function(...names, `return [${expr}];`) as (...a: unknown[]) => unknown[]
    return (params) => fn(...params)
  } catch {
    return (params) => params
  }
}

export function buildMethods(): Map<string, BridgeMethod> {
  const out = new Map<string, BridgeMethod>()
  for (const call of calls) {
    out.set(call.name, {
      name: call.name,
      transport: call.transport,
      channel: call.channel,
      args: compileArgs(call),
      params: call.params,
      returns: call.returns
    })
  }
  return out
}

export function contractEvents(): ContractEvent[] {
  return events
}

/** What `rpc.describe` answers: the callable surface and the event channels. */
export function describe(): {
  methods: {
    name: string
    transport: Transport
    channel?: string
    params: string[]
    returns: string
  }[]
  events: string[]
  local: string[]
} {
  return {
    methods: calls.map((c) => ({
      name: c.name,
      transport: c.transport,
      channel: c.channel,
      params: c.params.map((p) => `${p.name}${p.optional ? '?' : ''}: ${p.type}`),
      returns: c.returns
    })),
    events: events.map((e) => e.channel),
    local: Object.keys(LOCAL_VALUES)
  }
}
