import { describe, expect, it } from 'vitest'
import contract from '../../../../contracts/ui-api.json'
import {
  buildMethods,
  type ContractCall,
  compileArgs,
  describe as describeBridge
} from '../methods'

const calls = contract.calls as ContractCall[]

describe('ui-bridge methods (contract driven)', () => {
  it('exposes every call in the contract', () => {
    const methods = buildMethods()
    expect(methods.size).toBe(calls.length)
    for (const c of calls) expect(methods.has(c.name)).toBe(true)
  })

  it('every invoke/send call carries an IPC channel', () => {
    for (const c of calls) {
      if (c.transport === 'invoke' || c.transport === 'send') {
        expect(c.channel, c.name).toBeTruthy()
      }
    }
  })

  it('passes plain params straight through', () => {
    const save = calls.find((c) => c.name === 'projection.settings.save') as ContractCall
    expect(compileArgs(save)([{ darkMode: true }])).toEqual([{ darkMode: true }])
    const sel = calls.find((c) => c.name === 'projection.ipc.selectDevice') as ContractCall
    expect(compileArgs(sel)(['abc'])).toEqual(['abc'])
  })

  it('rebuilds the object shapes the preload wrappers send', () => {
    const touch = calls.find((c) => c.name === 'projection.ipc.sendTouch') as ContractCall
    expect(touch.channel).toBe('projection-touch')
    expect(compileArgs(touch)([100, 200, 0])).toEqual([{ x: 100, y: 200, action: 0 }])

    const fw = calls.find((c) => c.name === 'projection.ipc.dongleFirmware') as ContractCall
    expect(compileArgs(fw)(['check'])).toEqual([{ action: 'check' }])

    const vol = calls.find((c) => c.name === 'projection.ipc.setVolume') as ContractCall
    expect(compileArgs(vol)(['music', 0.5])).toEqual([{ stream: 'music', volume: 0.5 }])

    const vis = calls.find((c) => c.name === 'projection.ipc.setVisualizerEnabled') as ContractCall
    expect(compileArgs(vis)([1])).toEqual([true])
  })

  it('describe lists methods, events and local values', () => {
    const d = describeBridge()
    expect(d.methods.length).toBe(calls.length)
    expect(d.events).toContain('telemetry:update')
    expect(d.events).toContain('projection-event')
    expect(d.local).toEqual(['app.platform', 'app.compositor'])
  })
})
