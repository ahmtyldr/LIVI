import type { BoxInfo, SoftwareVersion } from '@projection/messages'
import type { ProjectionEvent } from '@projection/services/types'
import type { DevListEntry } from '@shared/types'

function isRecord(v: unknown): v is Record<string, unknown> {
  return v !== null && typeof v === 'object' && !Array.isArray(v)
}

// Dongle firmware reports MACs with either ':' or '-' separators.
function normPeerMac(v: string): string {
  return v.trim().replace(/-/g, ':').toUpperCase()
}

function asObject(input: unknown): Record<string, unknown> | null {
  if (!input) return null

  if (typeof input === 'object' && input !== null) return input as Record<string, unknown>

  if (typeof input === 'string') {
    const s = input.trim()
    if (!s) return null
    try {
      const parsed = JSON.parse(s)
      if (parsed && typeof parsed === 'object') return parsed as Record<string, unknown>
    } catch {
      // ignore
    }
  }

  return null
}

function isMeaningful(v: unknown): boolean {
  if (v == null) return false
  if (typeof v === 'string') return v.trim().length > 0
  return true
}

function mergePreferExisting(prev: unknown, next: unknown): unknown {
  const p = asObject(prev)
  const n = asObject(next)

  if (!p && !n) return next
  if (!p && n) return next
  if (p && !n) return prev

  // both objects
  const out: Record<string, unknown> = { ...p }

  for (const [k, v] of Object.entries(n!)) {
    if (isMeaningful(v)) {
      out[k] = v
    } else {
      // keep existing if present
      if (!(k in out)) out[k] = v
    }
  }

  return out
}

export type DongleStateDeps = {
  emit: (payload: ProjectionEvent) => void
  hasRenderer: () => boolean
  getHostDevList: () => DevListEntry[]
}

/**
 * Owns the dongle-reported state snapshot (firmware version, box info, its DevList
 * and connected MAC) and the de-duplicated dongleInfo emit that the picker reads.
 */
export class DongleState {
  private dongleFwVersion?: string
  private boxInfo?: unknown
  private dongleDevList: DevListEntry[] = []
  private dongleConnectedMac = ''
  private lastDongleInfoEmitKey = ''

  constructor(private readonly deps: DongleStateDeps) {}

  handleSoftwareVersion(msg: SoftwareVersion): void {
    this.dongleFwVersion = msg.version
    this.emitDongleInfoIfChanged()
  }

  handleBoxInfo(msg: BoxInfo): void {
    const settings = msg.settings as { DevList?: Array<Record<string, unknown>> }
    if (this.setDongleDevListFromSettings(settings)) {
      settings.DevList = this.mergedDevList() as unknown as Array<Record<string, unknown>>
    }
    const rawBtMac = (msg.settings as { btMacAddr?: unknown }).btMacAddr
    if (typeof rawBtMac === 'string' && rawBtMac.trim()) {
      this.dongleConnectedMac = normPeerMac(rawBtMac)
    }
    this.boxInfo = mergePreferExisting(this.boxInfo, msg.settings)
    this.emitDongleInfoIfChanged()
  }

  // Returns whether the dongle DevList was updated (caller re-emits devices).
  applyDongleInfo(info: { boxInfo?: unknown }): boolean {
    const settings = info.boxInfo as { DevList?: Array<Record<string, unknown>> } | undefined
    return Boolean(settings && this.setDongleDevListFromSettings(settings))
  }

  getDongleDevList(): DevListEntry[] {
    return this.dongleDevList
  }

  // Optimistic removal after a ForgetBluetoothAddr ack.
  removeFromDevList(btMac: string): boolean {
    const up = btMac.trim().toUpperCase()
    const next = this.dongleDevList.filter(
      (d) =>
        String(d.id ?? '')
          .trim()
          .toUpperCase() !== up
    )
    if (next.length === this.dongleDevList.length) return false
    this.dongleDevList = next
    return true
  }

  // The dongle pushes its paired list after pair/forget, fold it into the DevList.
  reconcileWithPairedRaw(raw: string): boolean {
    const macs = new Map<string, string>()
    for (const line of String(raw ?? '').split('\n')) {
      const trimmed = line.replace(/\r$/, '').replace(/\0+$/g, '')
      if (trimmed.length < 17) continue
      const mac = trimmed.slice(0, 17).toUpperCase()
      if (!mac.includes(':')) continue
      macs.set(mac, trimmed.slice(17).trim())
    }

    let changed = false
    const kept = this.dongleDevList.filter((d) => {
      const id = String(d.id ?? '')
        .trim()
        .toUpperCase()
      if (!id.includes(':')) return true
      const keep = macs.has(id)
      if (!keep) changed = true
      return keep
    })

    const known = new Set(
      kept.map((d) =>
        String(d.id ?? '')
          .trim()
          .toUpperCase()
      )
    )
    for (const [mac, name] of macs) {
      if (known.has(mac)) continue
      kept.push({ id: mac, name: name || undefined, source: 'dongle' })
      changed = true
    }

    if (changed) this.dongleDevList = kept
    return changed
  }

  getConnectedMac(): string {
    return this.dongleConnectedMac
  }

  // BluetoothPeerConnected updates the connected phone without waiting for a BoxInfo.
  setConnectedMac(mac: string): boolean {
    const next = normPeerMac(mac)
    if (!next || next === this.dongleConnectedMac) return false
    this.dongleConnectedMac = next
    return true
  }

  getFwVersion(): string | undefined {
    return this.dongleFwVersion
  }

  getBoxInfo(): unknown {
    return this.boxInfo
  }

  // Dongle unplugged: drop its state and emit the shrunken info unconditionally.
  clearOnDongleGone(): void {
    this.dongleDevList = []
    this.dongleConnectedMac = ''
    if (isRecord(this.boxInfo)) {
      this.boxInfo = { ...this.boxInfo, btMacAddr: '' }
    }
    this.deps.emit({
      type: 'dongleInfo',
      payload: {
        dongleFwVersion: this.dongleFwVersion,
        boxInfo: this.boxInfo
      }
    })
  }

  clearDongleSessionState(): void {
    this.dongleDevList = []
    this.dongleConnectedMac = ''
  }

  resetForTeardown(): void {
    this.dongleFwVersion = undefined
    if (isRecord(this.boxInfo)) {
      this.boxInfo = { ...this.boxInfo, btMacAddr: '' }
    }
    this.lastDongleInfoEmitKey = ''
  }

  invalidateDongleInfoKey(): void {
    this.lastDongleInfoEmitKey = ''
  }

  private setDongleDevListFromSettings(settings: {
    DevList?: Array<Record<string, unknown>>
  }): boolean {
    if (!Array.isArray(settings.DevList)) return false
    this.dongleDevList = settings.DevList.map((entry) => ({
      ...(entry as DevListEntry),
      source: 'dongle' as const
    }))
    return true
  }

  private mergedDevList(): DevListEntry[] {
    const norm = (id: string | undefined): string => (id ?? '').toUpperCase()
    const hostDevList = this.deps.getHostDevList()
    const hostMacs = new Set(hostDevList.map((e) => norm(e.id)))
    const dongleUnique = this.dongleDevList.filter((e) => !hostMacs.has(norm(e.id)))
    return [...hostDevList, ...dongleUnique]
  }

  private emitDongleInfoIfChanged(): void {
    if (!this.deps.hasRenderer()) return

    let boxKey = ''
    if (this.boxInfo != null) {
      try {
        boxKey = JSON.stringify(this.boxInfo)
      } catch {
        boxKey = String(this.boxInfo)
      }
    }

    const key = `${this.dongleFwVersion ?? ''}||${boxKey}`
    if (key === this.lastDongleInfoEmitKey) return
    this.lastDongleInfoEmitKey = key

    this.deps.emit({
      type: 'dongleInfo',
      payload: {
        dongleFwVersion: this.dongleFwVersion,
        boxInfo: this.boxInfo
      }
    })
  }
}
