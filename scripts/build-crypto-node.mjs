// Builds the livi-crypto N-API addon (crate livi-crypto-node) and places the cdylib
// where index.js loads it: native/crypto/build/Release/livi_crypto.node.
//
// Usage: node scripts/build-crypto-node.mjs [--arch=x64|arm64]
// Linux runners are arch-native; only macOS cross-compiles (arm64 host -> x64 app).
import { execFileSync } from 'node:child_process'
import { copyFileSync, mkdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const manifest = join(root, 'native', 'livi-helperd', 'Cargo.toml')

const archArg = process.argv.find((a) => a.startsWith('--arch='))?.slice(7)
const wantArch = archArg === 'x64' ? 'x64' : archArg === 'arm64' ? 'arm64' : process.arch
const cross = process.platform === 'darwin' && wantArch !== process.arch
const triple = wantArch === 'x64' ? 'x86_64-apple-darwin' : 'aarch64-apple-darwin'

const args = ['build', '--release', '-p', 'livi-crypto-node', '--manifest-path', manifest]
if (cross) {
  execFileSync('rustup', ['target', 'add', triple], { stdio: 'inherit' })
  args.push('--target', triple)
}
execFileSync('cargo', args, { stdio: 'inherit' })

const lib =
  process.platform === 'darwin'
    ? 'liblivi_crypto_node.dylib'
    : process.platform === 'win32'
      ? 'livi_crypto_node.dll'
      : 'liblivi_crypto_node.so'
const targetDir = join(root, 'native', 'livi-helperd', 'target', ...(cross ? [triple] : []), 'release')
const destDir = join(root, 'native', 'crypto', 'build', 'Release')
mkdirSync(destDir, { recursive: true })
copyFileSync(join(targetDir, lib), join(destDir, 'livi_crypto.node'))
console.log(`[build-crypto-node] ${lib} (${wantArch}) -> native/crypto/build/Release/livi_crypto.node`)
