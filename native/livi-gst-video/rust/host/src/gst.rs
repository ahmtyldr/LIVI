//! What the host reaches for when it runs for real: GStreamer pipelines and a
//! listening socket for the phone's screen stream.

use livi_screen_stream::ScreenSink;
use livi_video_player::Player;

use crate::{Outside, Plane};

impl Plane for Player {
    fn start(&self) {
        Player::start(self)
    }

    fn push(&self, nal: &[u8]) {
        Player::push(self, nal);
    }

    fn set_gamma(&self, gamma: f64, contrast: f64, r: f64, g: f64, b: f64) {
        Player::set_gamma(self, gamma, contrast, r, g, b)
    }
}

/// The listening socket, dropped when its receiver goes.
#[cfg(target_os = "linux")]
pub struct Ears(#[allow(dead_code)] livi_screen_stream::receiver::ScreenReceiver);

#[cfg(not(target_os = "linux"))]
pub struct Ears;

pub struct Gst;

impl Outside for Gst {
    type Plane = Player;
    type Ears = Ears;

    fn create_plane(&self, codec: &str, codec_data: &[u8]) -> Option<Player> {
        // the window comes from the sink, so the player needs no handle
        Player::new(codec, 0, codec_data)
    }

    #[cfg(target_os = "linux")]
    fn listen(&self, key: [u8; 32], sink: Box<dyn ScreenSink>) -> Option<(Ears, u16)> {
        match livi_screen_stream::receiver::ScreenReceiver::new(key, sink) {
            Ok((r, port)) => Some((Ears(r), port)),
            Err(e) => {
                eprintln!("[cp_screen] cannot listen: {e}");
                None
            }
        }
    }

    #[cfg(not(target_os = "linux"))]
    fn listen(&self, _key: [u8; 32], _sink: Box<dyn ScreenSink>) -> Option<(Ears, u16)> {
        None
    }
}

/// The codec support the main process picks its decoders from.
pub fn probe_json() -> String {
    livi_video_player::ensure_init();
    let mut out = String::from("{");
    for (i, codec) in ["h264", "h265", "vp9", "av1"].iter().enumerate() {
        let (hw, sw) = livi_video_player::probe(codec);
        if i > 0 {
            out.push(',');
        }
        out.push_str(&format!("\"{codec}\":{{\"hw\":{hw},\"sw\":{sw}}}"));
    }
    out.push('}');
    out
}
