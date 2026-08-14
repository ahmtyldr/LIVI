import { SettingsNode } from '@renderer/routes'
import type { Config } from '@shared/types'
import { ReactNode } from 'react'

export interface StackItemProps {
  children?: ReactNode
  withForwardIcon?: boolean
  value?: unknown
  showValue?: boolean
  onClick?: () => void
  node?: SettingsNode<Config>
  savedLabel?: string
  // Static rows opt into key navigation (focus stop + scroll anchor) without acting as buttons.
  focusable?: boolean
}

export type SettingsCustomPageProps<TState = Config, TValue = unknown> = {
  state: TState
  onChange: (value: TValue) => void
}
