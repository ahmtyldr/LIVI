import { createDecipheriv } from 'node:crypto'

const VENDOR_SESSION_KEY = Buffer.from('573245433158314e625a35385458746e', 'hex')

export function decodeVendorSessionAscii(raw: Buffer): string {
  return raw.toString('ascii').replace(/\0+$/g, '')
}

export function decodeVendorSessionBase64(ascii: string): Buffer {
  return Buffer.from(ascii, 'base64')
}

export async function decryptVendorSessionInfo(decoded: Buffer): Promise<Buffer> {
  if (decoded.length < 17) {
    throw new Error(`VendorSessionInfo too short: ${decoded.length}`)
  }

  const iv = decoded.subarray(0, 16)
  const ciphertext = decoded.subarray(16)

  const decipher = createDecipheriv('aes-128-cbc', VENDOR_SESSION_KEY, iv)
  return Buffer.concat([decipher.update(ciphertext), decipher.final()])
}

export async function decryptVendorSessionText(raw: Buffer): Promise<string> {
  const ascii = decodeVendorSessionAscii(raw)
  const decoded = decodeVendorSessionBase64(ascii)
  const decrypted = await decryptVendorSessionInfo(decoded)
  return decrypted.toString('utf8').replace(/\0+$/g, '').trim()
}
