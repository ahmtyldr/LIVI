import { fireEvent, render, waitFor } from '@testing-library/react'
import { DashShell } from '../DashShell'

function setWindowSize(w: number, h: number): void {
  Object.defineProperty(window, 'innerWidth', { configurable: true, value: w })
  Object.defineProperty(window, 'innerHeight', { configurable: true, value: h })
}

describe('DashShell', () => {
  beforeEach(async () => {
    vi.clearAllMocks()
    setWindowSize(1024, 768)
  })

  test('derives the initial scale from the window before resize information is available', async () => {
    const { container } = render(
      <DashShell>
        <div>Telemetry content</div>
      </DashShell>
    )

    expect(container.firstChild).toHaveStyle('--dash-scale: 0.8')
  })

  test('falls back to scale 1 while the window reports no size', async () => {
    setWindowSize(0, 0)
    const { container } = render(
      <DashShell>
        <div>Telemetry content</div>
      </DashShell>
    )

    expect(container.firstChild).toHaveStyle('--dash-scale: 1')
  })

  test('updates scale on window resize using the default design size', async () => {
    const { container } = render(
      <DashShell>
        <div>Telemetry content</div>
      </DashShell>
    )

    setWindowSize(640, 360)
    fireEvent(window, new Event('resize'))

    await waitFor(() => {
      expect(container.firstChild).toHaveStyle('--dash-scale: 0.5')
    })
  })

  test('updates scale on window resize using a custom design size', async () => {
    const { container } = render(
      <DashShell designWidth={1000} designHeight={500}>
        <div>Telemetry content</div>
      </DashShell>
    )

    setWindowSize(500, 400)
    fireEvent(window, new Event('resize'))

    await waitFor(() => {
      expect(container.firstChild).toHaveStyle('--dash-scale: 0.5')
    })
  })

  test('uses the smaller width/height ratio for scale', async () => {
    const { container } = render(
      <DashShell designWidth={1000} designHeight={500}>
        <div>Telemetry content</div>
      </DashShell>
    )

    setWindowSize(900, 200)
    fireEvent(window, new Event('resize'))

    await waitFor(() => {
      expect(container.firstChild).toHaveStyle('--dash-scale: 0.4')
    })
  })

  test('stops listening to window resize on unmount', async () => {
    const { container, unmount } = render(
      <DashShell>
        <div>Telemetry content</div>
      </DashShell>
    )

    unmount()

    setWindowSize(640, 360)
    fireEvent(window, new Event('resize'))

    expect(container.firstChild).toBeNull()
  })
})
