# native/crypto (livi-crypto)

N-API addon that gives the main process native ChaCha20-Poly1305, used for the
CarPlay handshake and to decrypt screen video frames without the JS AEAD's CPU
cost. One N-API build loads under both Electron and the test runner, so it must
be built (via `build:native:*` or `node scripts/build-crypto-node.mjs`) before
either runs.

Exports `open(key, nonce, ct, aad?)` and `seal(key, nonce, pt, aad?)`.

The addon is built with cargo from the Rust crate
`native/livi-helperd/crates/livi-crypto-node` (aws-lc-rs AEAD — assembly
ChaCha20/Poly1305, NEON on aarch64; needs cmake for the AWS-LC build). N-API is
ABI-stable, so no Electron headers are involved. The cdylib is copied to
`build/Release/livi_crypto.node`, where `index.js` loads it.

## livi_aead (used by the gst-video Linux build)

`livi_aead.c` / `livi_aead.h` implement RFC 8439 ChaCha20-Poly1305 (12-byte
nonce) open and seal on top of Monocypher's IETF ChaCha20 and Poly1305
primitives. `native/gst-video` compiles them into its Linux targets (the
`gst_video` addon and the `livi-gst-host` binary) for the screen stream.
Monocypher's own `crypto_aead_*` is XChaCha20 (24-byte nonce) and
does not match the wire format, so the construction is built here.

## Third-party licences

The compiled addon statically embeds [AWS-LC](https://github.com/aws/aws-lc)
via aws-lc-rs (ISC / Apache-2.0, with OpenSSL/SSLeay-licensed portions) and
[napi-rs](https://github.com/napi-rs/napi-rs) (MIT). The vendored Monocypher
sources below are covered by `LICENCE.md`.

## Monocypher (vendored)

`monocypher.c` / `monocypher.h` are vendored verbatim from Monocypher 4.0.2.

- Upstream: https://github.com/LoupVaillant/Monocypher
- Files: `src/monocypher.c`, `src/monocypher.h`
- Licence: dual CC0-1.0 / BSD-2-Clause, see `LICENCE.md`

To update, replace both files from the chosen release tag and update this note.
