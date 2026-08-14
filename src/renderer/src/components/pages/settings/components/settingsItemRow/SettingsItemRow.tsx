import { Typography } from '@mui/material'
import { SettingsNode } from '@renderer/routes'
import type { Config } from '@shared/types'
import { ReactNode } from 'react'
import { StackItem } from '../stackItem'

type Props = {
  label: string
  node?: SettingsNode<Config>
  children?: ReactNode
}

export const SettingsItemRow = ({ label, node, children }: Props) => {
  return (
    <StackItem node={node}>
      <Typography>{label}</Typography>
      {children}
    </StackItem>
  )
}
