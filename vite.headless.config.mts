// Builds the headless main process (src/main/headless.ts) for plain Node into
// out/main/headless.js. `electron` is aliased to the shim so the bundle never
// requires it; native addons stay external and resolve from node_modules.
import { builtinModules } from 'node:module'
import path, { resolve } from 'node:path'
import { defineConfig } from 'vite'

const NODE_BUILTINS = [...builtinModules, ...builtinModules.map((m) => `node:${m}`)]
const BUILD_SHA = (process.env.GITHUB_SHA || process.env.BUILD_SHA || 'dev').slice(0, 7)
const BUILD_RUN = process.env.GITHUB_RUN_NUMBER || process.env.BUILD_RUN || ''
const BUILD_BRANCH = process.env.BUILD_BRANCH || ''

export default defineConfig({
  define: {
    __BUILD_SHA__: JSON.stringify(BUILD_SHA),
    __BUILD_RUN__: JSON.stringify(BUILD_RUN),
    __BUILD_BRANCH__: JSON.stringify(BUILD_BRANCH)
  },
  resolve: {
    alias: {
      '@projection/messages': resolve(import.meta.dirname, 'src/main/services/projection/messages'),
      '@projection': resolve(import.meta.dirname, 'src/main/services/projection'),
      '@main': path.resolve(import.meta.dirname, 'src/main'),
      '@shared': path.resolve(import.meta.dirname, 'src/main/shared'),
      '@audio': path.resolve(import.meta.dirname, 'src/main/audio'),
      electron: resolve(import.meta.dirname, 'src/main/host/electron-shim.ts')
    }
  },
  build: {
    outDir: resolve(import.meta.dirname, 'out/main'),
    emptyOutDir: false,
    target: 'node24',
    minify: false,
    sourcemap: false,
    ssr: true,
    rolldownOptions: {
      input: { headless: resolve(import.meta.dirname, 'src/main/headless.ts') },
      external: [
        'usb',
        'livi-gst-video',
        'livi-crypto',
        'node-gyp-build',
        'protobufjs',
        ...NODE_BUILTINS
      ],
      output: {
        format: 'cjs',
        entryFileNames: '[name].js'
      }
    }
  },
  ssr: {
    target: 'node',
    noExternal: true
  }
})
