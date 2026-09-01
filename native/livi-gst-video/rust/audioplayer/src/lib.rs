//! One CarPlay audio stream from its RTP packets to the sink.
//!
//! The packets arrive decrypted from the receiver and go into an appsrc. The
//! jitter buffer paces them, the decoder turns them into PCM, and clocksync
//! releases each buffer at its running time, which the phone's burst delivery
//! needs.

pub mod uplink;

use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_controller as gst_controller;
use gstreamer_controller::prelude::*;

use livi_audio_stream::{reframe_aac, rtp_caps, Codec, RTP_HEADER_LEN};
use std::str::FromStr;
use std::sync::Once;

/// What a stream needs to build its pipeline.
pub struct Config {
    pub codec: Codec,
    pub payload_type: u8,
    pub clock_rate: u32,
    pub channels: u8,
    /// Jitter buffer depth. The phone ships buffered media that far ahead.
    pub latency_ms: u32,
    /// Voice and call streams take the short path to the sink.
    pub realtime: bool,
    pub device: Option<String>,
    pub label: String,
}

/// What turns the packets into raw samples. LPCM is already raw, so it goes
/// straight on.
fn decoder_chain(codec: Codec) -> Option<&'static str> {
    match codec {
        Codec::Lpcm | Codec::PcmLe => None,
        Codec::Opus => Some("rtpjitterbuffer latency={latency} ! rtpopusdepay ! opusdec"),
        #[cfg(target_os = "linux")]
        Codec::AacLc => Some("rtpjitterbuffer latency={latency} ! rtpmp4gdepay ! aacparse ! faad"),
        #[cfg(not(target_os = "linux"))]
        Codec::AacLc => {
            Some("rtpjitterbuffer latency={latency} ! rtpmp4gdepay ! aacparse ! avdec_aac")
        }
    }
}

/// The sink and its buffering. `sync=false` everywhere, since the pacing
/// happens upstream in the jitter buffer and clocksync.
fn sink_chain(cfg: &Config) -> String {
    let mut sink = if cfg!(target_os = "macos") {
        String::from("osxaudiosink")
    } else if cfg.realtime {
        String::from("pulsesink sync=false")
    } else {
        String::from("pulsesink sync=false buffer-time=300000 latency-time=30000")
    };
    if let Some(device) = &cfg.device {
        let prop = if cfg!(target_os = "macos") { "unique-id" } else { "device" };
        sink.push_str(&format!(" {prop}={device}"));
    }
    sink
}

/// The whole pipeline for `cfg`. The caps go onto the appsrc afterwards, so the
/// RFC 3640 fields keep their string types.
pub fn pipeline_desc(cfg: &Config) -> String {
    let head = match decoder_chain(cfg.codec) {
        Some(chain) => format!("{} ! ", chain.replace("{latency}", &cfg.latency_ms.max(100).to_string())),
        None => String::new(),
    };
    format!(
        "appsrc name=src is-live=true format=time do-timestamp=true ! \
         {head}audioconvert ! clocksync sync=true ! volume name=vol ! \
         audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! \
         queue ! {sink}",
        sink = sink_chain(cfg),
    )
}

pub struct Player {
    pipeline: gst::Pipeline,
    appsrc: gst_app::AppSrc,
    volume: gst::Element,
    /// GStreamer glides the level along this on the pipeline clock.
    level: gst_controller::InterpolationControlSource,
    codec: Codec,
    payload_type: u8,
}

impl Player {
    pub fn new(cfg: &Config) -> Option<Self> {
        ensure_init();

        let desc = pipeline_desc(cfg);
        let pipeline = match gst::parse::launch(&desc) {
            Ok(p) => p.downcast::<gst::Pipeline>().ok()?,
            Err(e) => {
                eprintln!("[cp_audio:{}] pipeline failed: {e}", cfg.label);
                return None;
            }
        };

        let appsrc = pipeline.by_name("src")?.downcast::<gst_app::AppSrc>().ok()?;
        let caps = gst::Caps::from_str(&rtp_caps(
            cfg.codec,
            cfg.payload_type,
            cfg.clock_rate,
            cfg.channels,
        ))
        .ok()?;
        appsrc.set_caps(Some(&caps));

        let volume = pipeline.by_name("vol")?;
        let level = gst_controller::InterpolationControlSource::new();
        level.set_mode(gst_controller::InterpolationMode::Linear);
        let binding = gst_controller::DirectControlBinding::new_absolute(&volume, "volume", &level);
        volume.add_control_binding(&binding).ok()?;
        volume.set_control_binding_disabled("volume", true);

        Some(Self {
            pipeline,
            appsrc,
            volume,
            level,
            codec: cfg.codec,
            payload_type: cfg.payload_type,
        })
    }

    pub fn start(&self) {
        let _ = self.pipeline.set_state(gst::State::Playing);
    }

    /// Feeds one decrypted RTP packet. False when the source no longer takes it.
    pub fn push_rtp(&self, rtp: &[u8]) -> bool {
        let packet = match self.codec {
            Codec::AacLc => reframe_aac(rtp, self.payload_type),
            Codec::Opus => rtp.to_vec(),
            // LPCM arrives without a header the pipeline needs
            Codec::Lpcm | Codec::PcmLe => rtp[RTP_HEADER_LEN.min(rtp.len())..].to_vec(),
        };
        self.appsrc.push_buffer(gst::Buffer::from_slice(packet)).is_ok()
    }

    /// Feeds samples the main process handed over, without an RTP header.
    pub fn push_samples(&self, samples: &[u8]) -> bool {
        self.appsrc.push_buffer(gst::Buffer::from_slice(samples.to_vec())).is_ok()
    }

    /// Sets the level at once, or glides to it over `ms` on the pipeline clock.
    pub fn set_volume(&self, level: f64, ms: u64) {
        let target = level.clamp(0.0, 10.0);
        let now = self.pipeline.current_running_time();

        if ms == 0 || now.is_none() {
            self.volume.set_control_binding_disabled("volume", true);
            self.volume.set_property("volume", target);
            return;
        }

        let now = now.unwrap();
        self.level.set(now, self.volume.property::<f64>("volume"));
        self.level.set(now + gst::ClockTime::from_mseconds(ms), target);
        self.volume.set_control_binding_disabled("volume", false);
    }
}

impl Drop for Player {
    fn drop(&mut self) {
        let _ = self.pipeline.set_state(gst::State::Null);
    }
}

/// Initialises GStreamer once for this process.
pub fn ensure_init() {
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        let _ = gst::init();
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg(codec: Codec, realtime: bool) -> Config {
        Config {
            codec,
            payload_type: 96,
            clock_rate: 44100,
            channels: 2,
            latency_ms: 1000,
            realtime,
            device: None,
            label: "music".into(),
        }
    }

    /// The elements in order, as the parser sees them.
    fn elements(desc: &str) -> Vec<String> {
        desc.split('!').map(|p| p.split_whitespace().next().unwrap_or("").to_string()).collect()
    }

    #[test]
    fn the_aac_chain_runs_from_appsrc_to_the_sink() {
        let desc = pipeline_desc(&cfg(Codec::AacLc, false));

        assert_eq!(
            elements(&desc),
            vec![
                "appsrc",
                "rtpjitterbuffer",
                "rtpmp4gdepay",
                "aacparse",
                if cfg!(target_os = "linux") { "faad" } else { "avdec_aac" },
                "audioconvert",
                "clocksync",
                "volume",
                "audioconvert",
                "audioresample",
                "audio/x-raw,format=S16LE,rate=48000,channels=2",
                "queue",
                if cfg!(target_os = "macos") { "osxaudiosink" } else { "pulsesink" },
            ]
        );
    }

    #[test]
    fn opus_depays_and_decodes_as_opus() {
        let desc = pipeline_desc(&cfg(Codec::Opus, false));
        let e = elements(&desc);

        assert!(e.contains(&"rtpopusdepay".to_string()));
        assert!(e.contains(&"opusdec".to_string()));
    }

    #[test]
    fn lpcm_goes_straight_from_the_source_to_the_converter() {
        let desc = pipeline_desc(&cfg(Codec::Lpcm, false));
        let e = elements(&desc);

        assert_eq!(e[0], "appsrc");
        assert_eq!(e[1], "audioconvert");
        assert!(!desc.contains("rtpjitterbuffer"));
        assert!(!desc.contains("depay"));
    }

    #[test]
    fn the_jitter_buffer_takes_the_negotiated_depth() {
        let desc = pipeline_desc(&cfg(Codec::AacLc, false));

        assert!(desc.contains("rtpjitterbuffer latency=1000"));
    }

    #[test]
    fn a_depth_below_the_floor_is_raised() {
        let mut c = cfg(Codec::Opus, false);
        c.latency_ms = 10;

        assert!(pipeline_desc(&c).contains("rtpjitterbuffer latency=100"));
    }

    #[cfg(not(target_os = "macos"))]
    #[test]
    fn a_music_sink_gets_the_ring_and_period_a_realtime_one_does_not() {
        assert!(pipeline_desc(&cfg(Codec::AacLc, false)).contains("buffer-time=300000"));
        assert!(!pipeline_desc(&cfg(Codec::AacLc, true)).contains("buffer-time"));
    }

    #[test]
    fn a_named_device_reaches_the_sink() {
        let mut c = cfg(Codec::Opus, false);
        c.device = Some("alsa_output.front".into());

        let desc = pipeline_desc(&c);
        let prop = if cfg!(target_os = "macos") { "unique-id" } else { "device" };
        assert!(desc.contains(&format!("{prop}=alsa_output.front")));
    }

    #[test]
    fn every_link_carries_a_space_on_both_sides() {
        let desc = pipeline_desc(&cfg(Codec::AacLc, false));

        assert_eq!(desc.matches(" ! ").count(), desc.matches('!').count());
    }
}
