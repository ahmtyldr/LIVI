//! The gst-host process: it serves the unix socket the main process connects
//! to, keeps the planes that process asks for, and feeds them from the CarPlay
//! screen receivers. The pipelines themselves live in the player crate.

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

use livi_host_proto::Framer;
use livi_screen_stream::ScreenSink;
use livi_video_fanout::Fanout;
use livi_video_nal::CpCodec;

pub mod gst;
#[cfg(target_os = "linux")]
pub mod process;

/// The receiver every cluster plane is fed from.
const CLUSTER_RECV_ID: u32 = 0x7a00_0010;
const CLUSTER_PLANE_MIN: u32 = 0x7a00_0011;
const CLUSTER_PLANE_MAX: u32 = 0x7a00_0013;

const OP_CREATE: u8 = 1;
const OP_DATA: u8 = 2;
const OP_STOP: u8 = 3;
const OP_GAMMA: u8 = 4;
const OP_LISTEN: u8 = 5;
const OP_TEARDOWN: u8 = 6;
const OP_SET_ACTIVE: u8 = 7;

const REPLY_PORT: u8 = 1;
const REPLY_CONFIG: u8 = 2;
const REPLY_STARTED: u8 = 3;

fn is_cluster_plane(id: u32) -> bool {
    (CLUSTER_PLANE_MIN..=CLUSTER_PLANE_MAX).contains(&id)
}

/// One decoding pipeline.
pub trait Plane: 'static {
    fn start(&self);
    fn push(&self, nal: &[u8]);
    fn set_gamma(&self, gamma: f64, contrast: f64, r: f64, g: f64, b: f64);
}

/// Everything the host reaches for outside itself: the decoding pipelines and
/// the port a phone sends its screen to. The process wires these to GStreamer
/// and to a listening socket, tests put stand-ins in their place.
pub trait Outside: 'static {
    type Plane: Plane;
    /// Kept for as long as its receiver. Dropping it stops the listening.
    type Ears;

    fn create_plane(&self, codec: &str, codec_data: &[u8]) -> Option<Self::Plane>;

    /// Starts listening and answers with the port to tell the phone about.
    fn listen(&self, key: [u8; 32], sink: Box<dyn ScreenSink>) -> Option<(Self::Ears, u16)>;
}

/// Where replies go: the socket in the process, a collector in tests.
pub trait Wire {
    fn reply(&self, op: u8, id: u32, rest: &[u8]);
}

type Planes<P> = Rc<RefCell<HashMap<u32, P>>>;

/// What a receiver and its feed share: which planes the frames are for, the
/// last configuration record, and the gate that decides what passes.
struct ReceiverState {
    plane_id: u32,
    is_cluster: bool,
    /// The codec byte, then the configuration atom.
    config: Vec<u8>,
    fan: Fanout,
}

impl ReceiverState {
    /// A cluster receiver serves every cluster plane, any other one serves the
    /// plane it was opened for.
    fn for_each_target<P: Plane>(&self, planes: &HashMap<u32, P>, mut f: impl FnMut(&P)) {
        if self.is_cluster {
            for id in CLUSTER_PLANE_MIN..=CLUSTER_PLANE_MAX {
                if let Some(p) = planes.get(&id) {
                    f(p);
                }
            }
        } else if let Some(p) = planes.get(&self.plane_id) {
            f(p);
        }
    }

    fn has_target<P: Plane>(&self, planes: &HashMap<u32, P>) -> bool {
        let mut any = false;
        self.for_each_target(planes, |_| any = true);
        any
    }
}

/// Carries what a receiver reads into the planes it serves.
struct Feed<O: Outside> {
    state: Rc<RefCell<ReceiverState>>,
    planes: Planes<O::Plane>,
    wire: Rc<dyn Wire>,
}

impl<O: Outside> ScreenSink for Feed<O> {
    fn on_config(&mut self, codec: CpCodec, atom: &[u8]) {
        let mut st = self.state.borrow_mut();
        st.fan.set_codec(codec);
        // a keepalive config carries no record, the last one stays
        if atom.is_empty() {
            return;
        }
        st.config.clear();
        st.config.push(codec as u8);
        st.config.extend_from_slice(atom);
        if st.fan.is_active() {
            self.wire.reply(REPLY_CONFIG, st.plane_id, &st.config);
        }
    }

    fn on_frame(&mut self, nal: &[u8]) {
        let planes = self.planes.borrow();
        let mut st = self.state.borrow_mut();
        let has_target = st.has_target(&planes);
        if st.fan.take(nal, has_target) {
            st.for_each_target(&planes, |p| p.push(nal));
        }
    }

    fn on_started(&mut self) {
        let st = self.state.borrow();
        if st.fan.is_active() {
            self.wire.reply(REPLY_STARTED, st.plane_id, &[]);
        }
    }
}

struct Receiver<E> {
    state: Rc<RefCell<ReceiverState>>,
    _ears: E,
}

/// Keeps the planes and the receivers, and acts on the messages the main
/// process sends.
pub struct Host<O: Outside> {
    outside: O,
    framer: Framer,
    planes: Planes<O::Plane>,
    receivers: HashMap<u32, Receiver<O::Ears>>,
    wire: Rc<dyn Wire>,
}

impl<O: Outside> Host<O> {
    pub fn new(outside: O, wire: Rc<dyn Wire>) -> Self {
        Self {
            outside,
            framer: Framer::new(),
            planes: Rc::new(RefCell::new(HashMap::new())),
            receivers: HashMap::new(),
            wire,
        }
    }

    /// Takes the next chunk from the socket and acts on every message it
    /// completes.
    pub fn feed(&mut self, chunk: &[u8]) {
        self.framer.push(chunk);
        while let Some(m) = self.framer.next_message() {
            self.dispatch(m.op, m.id, &m.rest);
        }
    }

    fn dispatch(&mut self, op: u8, id: u32, rest: &[u8]) {
        match op {
            OP_CREATE => self.create_plane(id, rest),
            OP_DATA => {
                if let Some(p) = self.planes.borrow().get(&id) {
                    p.push(rest);
                }
            }
            OP_STOP => {
                self.planes.borrow_mut().remove(&id);
            }
            OP_GAMMA => self.set_gamma(id, rest),
            OP_LISTEN => self.open_receiver(id, rest),
            OP_TEARDOWN => {
                self.receivers.remove(&id);
            }
            OP_SET_ACTIVE => self.set_active_feeder(id, rest),
            _ => {}
        }
    }

    /// `[1B codecLen][codec ascii][codec_data]`. A plane created while a
    /// receiver is already running is primed with the current GOP.
    fn create_plane(&mut self, id: u32, rest: &[u8]) {
        const CODEC_MAX: usize = 15;
        let clen = usize::from(*rest.first().unwrap_or(&0)).min(CODEC_MAX);
        if rest.len() < 1 + clen {
            return;
        }
        let codec = String::from_utf8_lossy(&rest[1..1 + clen]).into_owned();

        self.planes.borrow_mut().remove(&id);
        let Some(plane) = self.outside.create_plane(&codec, &rest[1 + clen..]) else {
            eprintln!("livi: create player 0x{id:x} (codec {codec}) FAILED");
            return;
        };
        plane.start();

        let feeder = if is_cluster_plane(id) { CLUSTER_RECV_ID } else { id };
        if let Some(state) = self.active_feeder(feeder) {
            for frame in state.borrow().fan.cached() {
                plane.push(frame);
            }
        }
        self.planes.borrow_mut().insert(id, plane);
    }

    /// The receiver currently feeding `plane_id`.
    fn active_feeder(&self, plane_id: u32) -> Option<&Rc<RefCell<ReceiverState>>> {
        self.receivers.values().map(|r| &r.state).find(|state| {
            let st = state.borrow();
            st.fan.is_active() && st.plane_id == plane_id
        })
    }

    /// Five doubles: gamma, contrast and the three gains.
    fn set_gamma(&mut self, id: u32, rest: &[u8]) {
        const N: usize = 5;
        if rest.len() < N * size_of::<f64>() {
            return;
        }
        let mut v = [0f64; N];
        for (i, slot) in v.iter_mut().enumerate() {
            let bytes = &rest[i * size_of::<f64>()..][..size_of::<f64>()];
            *slot = f64::from_ne_bytes(bytes.try_into().unwrap());
        }
        if let Some(p) = self.planes.borrow().get(&id) {
            p.set_gamma(v[0], v[1], v[2], v[3], v[4]);
        }
    }

    /// `[4B planeId][1B flags: bit0=cluster][32B key]`. The message id names
    /// the receiver, one per session and screen.
    fn open_receiver(&mut self, id: u32, rest: &[u8]) {
        const FLAGS: usize = 5;
        const KEY_LEN: usize = 32;
        if rest.len() < FLAGS + KEY_LEN {
            return;
        }

        let state = Rc::new(RefCell::new(ReceiverState {
            plane_id: u32::from_ne_bytes(rest[..4].try_into().unwrap()),
            is_cluster: rest[4] & 1 != 0,
            config: Vec::new(),
            fan: Fanout::new(),
        }));
        let feed = Feed::<O> {
            state: state.clone(),
            planes: self.planes.clone(),
            wire: self.wire.clone(),
        };
        let key: [u8; KEY_LEN] = rest[FLAGS..FLAGS + KEY_LEN].try_into().unwrap();
        let Some((ears, port)) = self.outside.listen(key, Box::new(feed)) else {
            return;
        };

        self.receivers.insert(id, Receiver { state, _ears: ears });
        self.wire.reply(REPLY_PORT, id, &port.to_le_bytes());
    }

    /// `[1B active]`. Making a receiver active makes every other receiver of the
    /// same plane passive, so one screen has one feeder.
    fn set_active_feeder(&mut self, id: u32, rest: &[u8]) {
        let Some(r) = self.receivers.get(&id) else {
            return;
        };
        let plane_id = r.state.borrow().plane_id;

        if !rest.first().is_some_and(|b| b & 1 != 0) {
            r.state.borrow_mut().fan.set_active(false);
            return;
        }

        for (other, o) in &self.receivers {
            let same_plane = o.state.borrow().plane_id == plane_id;
            if *other != id && same_plane {
                o.state.borrow_mut().fan.set_active(false);
            }
        }

        let mut st = r.state.borrow_mut();
        st.fan.set_active(true);
        st.fan.restart();
        if !st.config.is_empty() {
            self.wire.reply(REPLY_CONFIG, plane_id, &st.config);
        }
    }

    /// What every receiver saw in the last window. Reading it starts the
    /// counters over.
    pub fn take_stats(&self) -> Vec<String> {
        let mut lines = Vec::new();
        for r in self.receivers.values() {
            let mut st = r.state.borrow_mut();
            let awaiting = u8::from(st.fan.awaiting_keyframe());
            let active = u8::from(st.fan.is_active());
            let s = st.fan.take_stats();
            if s.incoming == 0 && s.dropped == 0 && s.pushed == 0 {
                continue;
            }
            lines.push(format!(
                "[cp_screen] recv 0x{:x}: in={} dropped={} pushed={} awaiting_kf={awaiting} active={active}",
                st.plane_id, s.incoming, s.dropped, s.pushed
            ));
        }
        lines
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MAIN_PLANE: u32 = 0x7a00_0001;
    const KEYFRAME: u8 = 5;
    const DELTA: u8 = 1;

    #[derive(Default)]
    struct PlaneLog {
        started: usize,
        pushed: Vec<Vec<u8>>,
        gamma: Option<[f64; 5]>,
    }

    /// A plane that writes down what it was told to do.
    #[derive(Default, Clone)]
    struct FakePlane(Rc<RefCell<PlaneLog>>);

    impl FakePlane {
        fn started(&self) -> usize {
            self.0.borrow().started
        }

        fn pushed(&self) -> Vec<Vec<u8>> {
            self.0.borrow().pushed.clone()
        }

        fn gamma(&self) -> Option<[f64; 5]> {
            self.0.borrow().gamma
        }
    }

    impl Plane for FakePlane {
        fn start(&self) {
            self.0.borrow_mut().started += 1;
        }

        fn push(&self, nal: &[u8]) {
            self.0.borrow_mut().pushed.push(nal.to_vec());
        }

        fn set_gamma(&self, gamma: f64, contrast: f64, r: f64, g: f64, b: f64) {
            self.0.borrow_mut().gamma = Some([gamma, contrast, r, g, b]);
        }
    }

    type SharedSink = Rc<RefCell<Box<dyn ScreenSink>>>;

    #[derive(Default)]
    struct World {
        planes: Vec<FakePlane>,
        codecs: Vec<(String, Vec<u8>)>,
        sinks: Vec<SharedSink>,
        keys: Vec<[u8; 32]>,
        refuse_plane: bool,
        deaf: bool,
        port: u16,
    }

    /// The world the host talks to, and the test's handle on it.
    #[derive(Default, Clone)]
    struct Fake(Rc<RefCell<World>>);

    impl Outside for Fake {
        type Plane = FakePlane;
        type Ears = SharedSink;

        fn create_plane(&self, codec: &str, codec_data: &[u8]) -> Option<FakePlane> {
            if self.0.borrow().refuse_plane {
                return None;
            }
            let plane = FakePlane::default();
            let mut w = self.0.borrow_mut();
            w.codecs.push((codec.to_owned(), codec_data.to_vec()));
            w.planes.push(plane.clone());
            Some(plane)
        }

        fn listen(&self, key: [u8; 32], sink: Box<dyn ScreenSink>) -> Option<(SharedSink, u16)> {
            if self.0.borrow().deaf {
                return None;
            }
            let shared: SharedSink = Rc::new(RefCell::new(sink));
            let mut w = self.0.borrow_mut();
            w.keys.push(key);
            w.sinks.push(shared.clone());
            Some((shared, w.port))
        }
    }

    type Reply = (u8, u32, Vec<u8>);

    /// Collects the replies that would go down the socket.
    #[derive(Default, Clone)]
    struct Sent(Rc<RefCell<Vec<Reply>>>);

    impl Wire for Sent {
        fn reply(&self, op: u8, id: u32, rest: &[u8]) {
            self.0.borrow_mut().push((op, id, rest.to_vec()));
        }
    }

    fn frame(op: u8, id: u32, rest: &[u8]) -> Vec<u8> {
        let mut v = ((HEAD + rest.len()) as u32).to_ne_bytes().to_vec();
        v.push(op);
        v.extend_from_slice(&id.to_ne_bytes());
        v.extend_from_slice(rest);
        v
    }

    const HEAD: usize = 5;

    fn create_body(codec: &str, codec_data: &[u8]) -> Vec<u8> {
        let mut v = vec![codec.len() as u8];
        v.extend_from_slice(codec.as_bytes());
        v.extend_from_slice(codec_data);
        v
    }

    fn listen_body(plane_id: u32, cluster: bool, key: u8) -> Vec<u8> {
        let mut v = plane_id.to_ne_bytes().to_vec();
        v.push(u8::from(cluster));
        v.extend_from_slice(&[key; 32]);
        v
    }

    fn gamma_body(v: [f64; 5]) -> Vec<u8> {
        v.iter().flat_map(|d| d.to_ne_bytes()).collect()
    }

    /// One access unit: a four-byte length, the NAL header byte, and a marker
    /// that tells the frames apart.
    fn nal(kind: u8, mark: u8) -> Vec<u8> {
        let mut v = 2u32.to_be_bytes().to_vec();
        v.push(kind);
        v.push(mark);
        v
    }

    struct Fixture {
        host: Host<Fake>,
        world: Fake,
        sent: Sent,
    }

    impl Fixture {
        fn new() -> Self {
            let world = Fake::default();
            world.0.borrow_mut().port = 5555;
            let sent = Sent::default();
            let host = Host::new(world.clone(), Rc::new(sent.clone()));
            Self { host, world, sent }
        }

        fn send(&mut self, op: u8, id: u32, rest: &[u8]) {
            self.host.feed(&frame(op, id, rest));
        }

        fn plane(&self, i: usize) -> FakePlane {
            self.world.0.borrow().planes[i].clone()
        }

        fn planes(&self) -> usize {
            self.world.0.borrow().planes.len()
        }

        fn sink(&self, i: usize) -> SharedSink {
            self.world.0.borrow().sinks[i].clone()
        }

        fn config(&self, i: usize, codec: CpCodec, atom: &[u8]) {
            self.sink(i).borrow_mut().on_config(codec, atom);
        }

        fn frame_in(&self, i: usize, nal: &[u8]) {
            self.sink(i).borrow_mut().on_frame(nal);
        }

        fn started_in(&self, i: usize) {
            self.sink(i).borrow_mut().on_started();
        }

        fn replies(&self) -> Vec<Reply> {
            self.sent.0.borrow().clone()
        }

        /// Opens a receiver on `recv_id` for `plane_id` and makes it the feeder.
        fn feeder(&mut self, recv_id: u32, plane_id: u32, cluster: bool) {
            self.send(OP_LISTEN, recv_id, &listen_body(plane_id, cluster, 1));
            self.send(OP_SET_ACTIVE, recv_id, &[1]);
        }
    }

    #[test]
    fn a_create_message_starts_a_plane_and_data_reaches_it() {
        let mut f = Fixture::new();

        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[9, 9]));
        f.send(OP_DATA, MAIN_PLANE, &[1, 2, 3]);

        assert_eq!(f.world.0.borrow().codecs, vec![("h264".to_owned(), vec![9, 9])]);
        assert_eq!(f.plane(0).started(), 1);
        assert_eq!(f.plane(0).pushed(), vec![vec![1, 2, 3]]);
    }

    #[test]
    fn creating_a_plane_twice_replaces_the_first() {
        let mut f = Fixture::new();

        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h265", &[]));
        f.send(OP_DATA, MAIN_PLANE, &[7]);

        assert_eq!(f.planes(), 2);
        assert!(f.plane(0).pushed().is_empty());
        assert_eq!(f.plane(1).pushed(), vec![vec![7]]);
    }

    #[test]
    fn a_stop_message_drops_the_plane() {
        let mut f = Fixture::new();
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.send(OP_STOP, MAIN_PLANE, &[]);
        f.send(OP_DATA, MAIN_PLANE, &[7]);

        assert!(f.plane(0).pushed().is_empty());
    }

    #[test]
    fn a_create_message_shorter_than_its_codec_name_is_ignored() {
        let mut f = Fixture::new();

        f.send(OP_CREATE, MAIN_PLANE, &[8, b'h']);

        assert_eq!(f.planes(), 0);
    }

    #[test]
    fn a_plane_that_cannot_be_built_leaves_nothing_behind() {
        let mut f = Fixture::new();
        f.world.0.borrow_mut().refuse_plane = true;

        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        f.send(OP_DATA, MAIN_PLANE, &[7]);

        assert_eq!(f.planes(), 0);
    }

    #[test]
    fn gamma_reaches_the_plane() {
        let mut f = Fixture::new();
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.send(OP_GAMMA, MAIN_PLANE, &gamma_body([1.5, 2.0, 0.5, 0.6, 0.7]));

        assert_eq!(f.plane(0).gamma(), Some([1.5, 2.0, 0.5, 0.6, 0.7]));
    }

    #[test]
    fn a_gamma_message_short_of_five_values_is_ignored() {
        let mut f = Fixture::new();
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.send(OP_GAMMA, MAIN_PLANE, &gamma_body([1.5, 2.0, 0.5, 0.6, 0.7])[..39]);

        assert_eq!(f.plane(0).gamma(), None);
    }

    #[test]
    fn an_unknown_opcode_is_ignored() {
        let mut f = Fixture::new();

        f.send(9, MAIN_PLANE, &[1, 2, 3]);

        assert_eq!(f.planes(), 0);
        assert!(f.replies().is_empty());
    }

    #[test]
    fn two_messages_in_one_chunk_are_both_acted_on() {
        let mut f = Fixture::new();
        let mut chunk = frame(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        chunk.extend(frame(OP_DATA, MAIN_PLANE, &[4]));

        f.host.feed(&chunk);

        assert_eq!(f.plane(0).pushed(), vec![vec![4]]);
    }

    #[test]
    fn a_message_split_across_chunks_waits_for_the_rest() {
        let mut f = Fixture::new();
        let msg = frame(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.host.feed(&msg[..6]);
        assert_eq!(f.planes(), 0);

        f.host.feed(&msg[6..]);
        assert_eq!(f.planes(), 1);
    }

    #[test]
    fn listening_answers_with_the_port_and_takes_the_key() {
        let mut f = Fixture::new();

        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 3));

        assert_eq!(f.replies(), vec![(REPLY_PORT, 42, 5555u16.to_le_bytes().to_vec())]);
        assert_eq!(f.world.0.borrow().keys, vec![[3u8; 32]]);
    }

    #[test]
    fn a_listen_message_short_of_the_key_opens_nothing() {
        let mut f = Fixture::new();

        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 3)[..36]);

        assert!(f.replies().is_empty());
        assert!(f.world.0.borrow().sinks.is_empty());
    }

    #[test]
    fn a_receiver_that_cannot_listen_is_not_announced() {
        let mut f = Fixture::new();
        f.world.0.borrow_mut().deaf = true;

        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 3));

        assert!(f.replies().is_empty());
    }

    #[test]
    fn frames_reach_the_plane_the_receiver_serves() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));
        f.frame_in(0, &nal(DELTA, 2));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 1), nal(DELTA, 2)]);
    }

    #[test]
    fn frames_before_a_keyframe_are_dropped() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(DELTA, 1));

        assert!(f.plane(0).pushed().is_empty());
    }

    #[test]
    fn a_passive_receiver_feeds_nothing() {
        let mut f = Fixture::new();
        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 1));
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));

        assert!(f.plane(0).pushed().is_empty());
    }

    #[test]
    fn a_cluster_receiver_feeds_every_cluster_plane() {
        let mut f = Fixture::new();
        f.feeder(CLUSTER_RECV_ID, CLUSTER_RECV_ID, true);
        f.send(OP_CREATE, CLUSTER_PLANE_MIN, &create_body("h264", &[]));
        f.send(OP_CREATE, CLUSTER_PLANE_MAX, &create_body("h264", &[]));

        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 1)]);
        assert_eq!(f.plane(1).pushed(), vec![nal(KEYFRAME, 1)]);
    }

    #[test]
    fn a_plane_created_mid_stream_is_primed_with_the_cached_gop() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));
        f.frame_in(0, &nal(DELTA, 2));

        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 1), nal(DELTA, 2)]);
    }

    #[test]
    fn a_cluster_plane_is_primed_from_the_cluster_receiver() {
        let mut f = Fixture::new();
        f.feeder(CLUSTER_RECV_ID, CLUSTER_RECV_ID, true);
        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));

        f.send(OP_CREATE, CLUSTER_PLANE_MIN, &create_body("h264", &[]));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 1)]);
    }

    #[test]
    fn a_torn_down_receiver_no_longer_primes_new_planes() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));

        f.send(OP_TEARDOWN, 42, &[]);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));

        assert!(f.plane(0).pushed().is_empty());
    }

    #[test]
    fn activating_a_receiver_makes_the_other_one_of_the_plane_passive() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        f.config(0, CpCodec::H264, &[1, 2]);

        f.feeder(43, MAIN_PLANE, false);
        f.config(1, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(KEYFRAME, 1));
        f.frame_in(1, &nal(KEYFRAME, 2));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 2)]);
    }

    #[test]
    fn a_receiver_of_another_plane_stays_active() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        f.config(0, CpCodec::H264, &[1, 2]);

        f.feeder(43, CLUSTER_RECV_ID, true);
        f.frame_in(0, &nal(KEYFRAME, 1));

        assert_eq!(f.plane(0).pushed(), vec![nal(KEYFRAME, 1)]);
    }

    #[test]
    fn deactivating_a_receiver_stops_its_feed() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.send(OP_CREATE, MAIN_PLANE, &create_body("h264", &[]));
        f.config(0, CpCodec::H264, &[1, 2]);

        f.send(OP_SET_ACTIVE, 42, &[0]);
        f.frame_in(0, &nal(KEYFRAME, 1));

        assert!(f.plane(0).pushed().is_empty());
    }

    #[test]
    fn setting_a_receiver_that_does_not_exist_does_nothing() {
        let mut f = Fixture::new();

        f.send(OP_SET_ACTIVE, 42, &[1]);

        assert!(f.replies().is_empty());
    }

    #[test]
    fn a_configuration_is_forwarded_while_the_receiver_is_active() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);

        f.config(0, CpCodec::H265, &[1, 2]);

        assert_eq!(
            f.replies().last(),
            Some(&(REPLY_CONFIG, MAIN_PLANE, vec![CpCodec::H265 as u8, 1, 2]))
        );
    }

    #[test]
    fn activating_a_receiver_forwards_the_configuration_it_already_had() {
        let mut f = Fixture::new();
        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 1));
        f.config(0, CpCodec::H264, &[7, 8]);

        f.send(OP_SET_ACTIVE, 42, &[1]);

        assert_eq!(
            f.replies().last(),
            Some(&(REPLY_CONFIG, MAIN_PLANE, vec![CpCodec::H264 as u8, 7, 8]))
        );
    }

    #[test]
    fn activating_a_receiver_without_a_configuration_forwards_nothing() {
        let mut f = Fixture::new();
        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 1));

        f.send(OP_SET_ACTIVE, 42, &[1]);

        assert_eq!(f.replies().len(), 1);
    }

    #[test]
    fn a_keepalive_configuration_keeps_the_last_record() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.config(0, CpCodec::H264, &[7, 8]);

        f.config(0, CpCodec::H264, &[]);
        f.send(OP_SET_ACTIVE, 42, &[1]);

        assert_eq!(
            f.replies().last(),
            Some(&(REPLY_CONFIG, MAIN_PLANE, vec![CpCodec::H264 as u8, 7, 8]))
        );
    }

    #[test]
    fn the_start_of_a_stream_is_reported_while_the_receiver_is_active() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);

        f.started_in(0);

        assert_eq!(f.replies().last(), Some(&(REPLY_STARTED, MAIN_PLANE, vec![])));
    }

    #[test]
    fn a_passive_receiver_reports_no_start() {
        let mut f = Fixture::new();
        f.send(OP_LISTEN, 42, &listen_body(MAIN_PLANE, false, 1));

        f.started_in(0);

        assert_eq!(f.replies().len(), 1);
    }

    #[test]
    fn the_statistics_name_the_plane_and_start_over_when_read() {
        let mut f = Fixture::new();
        f.feeder(42, MAIN_PLANE, false);
        f.config(0, CpCodec::H264, &[1, 2]);
        f.frame_in(0, &nal(DELTA, 1));

        let first = f.host.take_stats();

        assert_eq!(first.len(), 1);
        assert!(first[0].contains("recv 0x7a000001: in=1 dropped=1 pushed=0"));
        assert!(first[0].ends_with("awaiting_kf=1 active=1"));
        assert!(f.host.take_stats().is_empty());
    }
}
