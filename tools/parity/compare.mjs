#!/usr/bin/env node
// Pixel parity between an Electron reference capture and a livi-ui capture.
//
//   node tools/parity/compare.mjs ref.png test.png [--out diff.png]
//        [--region x,y,w,h]... [--tolerance 24] [--max 1]
//
// A pixel differs when any channel differs by more than --tolerance.
// Prints the differing fraction for the whole frame and each --region
// (name=x,y,w,h), writes a diff image (reference dimmed, differences in
// red) and exits 1 when the whole-frame fraction exceeds --max percent.
import { readFileSync, writeFileSync } from 'node:fs'
import { decodePng, encodePng } from './png.mjs'

const args = process.argv.slice(2)
const files = []
let out, tolerance = 24, max = 1
const regions = []
for (let i = 0; i < args.length; i++) {
  const a = args[i]
  if (a === '--out') out = args[++i]
  else if (a === '--tolerance') tolerance = Number(args[++i])
  else if (a === '--max') max = Number(args[++i])
  else if (a === '--region') {
    const spec = args[++i]
    const [name, geom] = spec.includes('=') ? spec.split('=') : ['region', spec]
    const [x, y, w, h] = geom.split(',').map(Number)
    regions.push({ name, x, y, w, h })
  } else files.push(a)
}
if (files.length !== 2) {
  console.error('usage: compare.mjs ref.png test.png [--out diff.png] [--region name=x,y,w,h] [--tolerance N] [--max PCT]')
  process.exit(2)
}
const ref = decodePng(readFileSync(files[0]))
const test = decodePng(readFileSync(files[1]))
if (ref.width !== test.width || ref.height !== test.height) {
  console.error(`size mismatch: ${ref.width}x${ref.height} vs ${test.width}x${test.height}`)
  process.exit(2)
}
const { width, height } = ref
const diffMask = new Uint8Array(width * height)
let total = 0
for (let i = 0; i < width * height; i++) {
  const o = i * 4
  const d = Math.max(
    Math.abs(ref.data[o] - test.data[o]),
    Math.abs(ref.data[o + 1] - test.data[o + 1]),
    Math.abs(ref.data[o + 2] - test.data[o + 2])
  )
  if (d > tolerance) { diffMask[i] = 1; total++ }
}
const pct = (n, d) => ((100 * n) / d).toFixed(3)
const wholePct = (100 * total) / (width * height)
console.log(`frame ${width}x${height}: ${total} px differ (${pct(total, width * height)} %)`)
for (const r of regions) {
  let n = 0
  for (let y = r.y; y < r.y + r.h; y++) for (let x = r.x; x < r.x + r.w; x++) n += diffMask[y * width + x]
  console.log(`  ${r.name} (${r.x},${r.y} ${r.w}x${r.h}): ${n} px (${pct(n, r.w * r.h)} %)`)
}
// bounding box of differences helps aim the next fix
if (total) {
  let x0 = width, y0 = height, x1 = 0, y1 = 0
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) if (diffMask[y * width + x]) {
    if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y
  }
  console.log(`  bbox: ${x0},${y0} → ${x1},${y1}`)
}
if (out) {
  const data = new Uint8Array(width * height * 4)
  for (let i = 0; i < width * height; i++) {
    const o = i * 4
    if (diffMask[i]) { data[o] = 255; data[o + 1] = 0; data[o + 2] = 0 }
    else { const g = (ref.data[o] + ref.data[o + 1] + ref.data[o + 2]) / 3; data[o] = data[o + 1] = data[o + 2] = 64 + g * 0.5 }
    data[o + 3] = 255
  }
  writeFileSync(out, encodePng({ width, height, data }))
  console.log(`diff → ${out}`)
}
process.exit(wholePct > max ? 1 : 0)
