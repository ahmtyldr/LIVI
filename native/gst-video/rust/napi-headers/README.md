# napi-headers (vendored)

N-API C headers, vendored verbatim from Node.js (`include/node/`, MIT licence).
`build.rs` puts them on the include path for `gst_video.cc`; N-API is ABI-stable,
so one copy serves every Node and Electron version with `NAPI_VERSION >= 8`.

- Upstream: https://github.com/nodejs/node/tree/main/src
- Files: `node_api.h`, `node_api_types.h`, `js_native_api.h`, `js_native_api_types.h`

To update, replace the four files from a Node release and update this note.
