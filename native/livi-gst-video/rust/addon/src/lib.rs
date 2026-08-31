//! N-API surface for the gst_video addon: everything here converts between
//! JavaScript values and the crates the pipeline lives in.

use napi::bindgen_prelude::{Buffer, External};
use napi_derive::napi;

use livi_video_player::Player;

unsafe extern "C" {
    // The window backdrop is platform code in gst_video_mac.mm.
    #[cfg(not(target_os = "linux"))]
    fn livi_set_backdrop(parent: usize, r: f64, g: f64, b: f64);
}

#[napi(object)]
pub struct CodecSupport {
    pub hw: bool,
    pub sw: bool,
}

#[napi(object)]
pub struct CodecProbe {
    pub h264: CodecSupport,
    pub h265: CodecSupport,
    pub vp9: CodecSupport,
    pub av1: CodecSupport,
}

/// The GStreamer the pipeline runs on.
#[napi]
pub fn version() -> String {
    livi_video_player::version()
}

fn probe(codec: &str) -> CodecSupport {
    let (hw, sw) = livi_video_player::probe(codec);
    CodecSupport { hw, sw }
}

/// Whether a hardware and a software decoder exist, per codec.
#[napi]
pub fn probe_codecs() -> CodecProbe {
    CodecProbe {
        h264: probe("h264"),
        h265: probe("h265"),
        vp9: probe("vp9"),
        av1: probe("av1"),
    }
}

/// The first pointer-sized bytes of the buffer, which carry the window handle.
fn window_handle(buf: &Buffer) -> usize {
    let bytes: &[u8] = buf.as_ref();
    if bytes.len() < core::mem::size_of::<usize>() {
        return 0;
    }
    let mut raw = [0u8; core::mem::size_of::<usize>()];
    raw.copy_from_slice(&bytes[..core::mem::size_of::<usize>()]);
    usize::from_ne_bytes(raw)
}

/// Builds the pipeline for `codec` into the given window. Null when it fails.
#[napi]
pub fn create_player(
    codec: String,
    window_handle_buf: Buffer,
    codec_data: Option<Buffer>,
) -> Option<External<Player>> {
    let cd: &[u8] = codec_data.as_ref().map_or(&[], |b| b.as_ref());
    Player::new(&codec, window_handle(&window_handle_buf), cd).map(External::new)
}

#[napi]
pub fn start(player: External<Player>) {
    player.start()
}

/// Feeds one buffer. False when the player cannot take it.
#[napi]
pub fn push_buffer(player: External<Player>, buffer: Buffer) -> bool {
    let bytes: &[u8] = buffer.as_ref();
    if bytes.is_empty() {
        return false;
    }
    player.push(bytes)
}

#[napi]
pub fn set_visible(player: External<Player>, visible: bool) {
    player.set_visible(visible)
}

#[napi]
pub fn stop(mut player: External<Player>) {
    player.stop()
}

#[napi]
#[allow(clippy::too_many_arguments)]
pub fn set_content_region(
    player: External<Player>,
    crop_l: f64,
    crop_t: f64,
    vis_w: f64,
    vis_h: f64,
    tier_w: f64,
    tier_h: f64,
) {
    player.set_content_region(crop_l, crop_t, vis_w, vis_h, tier_w, tier_h)
}

#[napi]
pub fn set_backdrop(window_handle_buf: Buffer, r: f64, g: f64, b: f64) {
    let handle = window_handle(&window_handle_buf);
    if handle == 0 {
        return;
    }
    #[cfg(not(target_os = "linux"))]
    unsafe {
        livi_set_backdrop(handle, r, g, b)
    };
    #[cfg(target_os = "linux")]
    let _ = (r, g, b);
}

#[napi]
pub fn set_gamma(
    player: External<Player>,
    gamma: f64,
    contrast: f64,
    r: f64,
    g: f64,
    b: f64,
) {
    player.set_gamma(gamma, contrast, r, g, b)
}

