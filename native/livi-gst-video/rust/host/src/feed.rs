//! The socket a helper streams media into. One connection at a time, every
//! record goes to the sink on the main loop, like the screen receiver does it.

use std::cell::RefCell;
use std::io::Read;
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::rc::Rc;

use glib::IOCondition;
use livi_host_proto::feed::Framer;

use crate::MediaSink;

type Client = Rc<RefCell<Option<(u64, UnixStream)>>>;
type Sink = Rc<RefCell<Box<dyn MediaSink>>>;

pub struct FeedListener {
    path: String,
    listener: UnixListener,
    sources: Vec<glib::SourceId>,
    client: Client,
}

impl FeedListener {
    pub fn new(path: &str, sink: Box<dyn MediaSink>) -> std::io::Result<Self> {
        let _ = std::fs::remove_file(path);
        let listener = UnixListener::bind(path)?;
        listener.set_nonblocking(true)?;
        let mut l = Self {
            path: path.to_owned(),
            listener,
            sources: Vec::new(),
            client: Rc::new(RefCell::new(None)),
        };
        l.watch_listener(Rc::new(RefCell::new(sink)));
        Ok(l)
    }

    fn watch_listener(&mut self, sink: Sink) {
        let fd = self.listener.as_raw_fd();
        let listener = self.listener.try_clone().expect("dup listener");
        let client = self.client.clone();
        let mut generation = 0u64;

        let id = glib::unix_fd_add_local(fd, IOCondition::IN, move |_, _| {
            let Ok((sock, _)) = listener.accept() else {
                return glib::ControlFlow::Continue;
            };
            let _ = sock.set_nonblocking(true);
            // a new helper connection replaces the one before it
            generation += 1;
            let cfd = sock.as_raw_fd();
            *client.borrow_mut() = Some((generation, sock));
            watch_client(cfd, generation, client.clone(), sink.clone());
            eprintln!("[feed] connection accepted");
            glib::ControlFlow::Continue
        });
        self.sources.push(id);
    }
}

fn watch_client(fd: RawFd, generation: u64, client: Client, sink: Sink) {
    let cond = IOCondition::IN | IOCondition::HUP | IOCondition::ERR;
    let mut framer = Framer::new();
    glib::unix_fd_add_local(fd, cond, move |_, cond| {
        let mut chunk = [0u8; 65536];
        let read = {
            let mut guard = client.borrow_mut();
            match guard.as_mut() {
                Some((g, sock)) if *g == generation => {
                    if cond.contains(IOCondition::IN) {
                        sock.read(&mut chunk).ok().filter(|n| *n > 0)
                    } else {
                        None
                    }
                }
                // replaced by a newer connection
                _ => return glib::ControlFlow::Break,
            }
        };
        match read {
            Some(n) => {
                framer.push(&chunk[..n]);
                let mut s = sink.borrow_mut();
                while let Some(record) = framer.next_record() {
                    s.on_record(record);
                }
                glib::ControlFlow::Continue
            }
            None => {
                let mut guard = client.borrow_mut();
                if matches!(guard.as_ref(), Some((g, _)) if *g == generation) {
                    *guard = None;
                    eprintln!("[feed] connection closed");
                }
                glib::ControlFlow::Break
            }
        }
    });
}

impl Drop for FeedListener {
    fn drop(&mut self) {
        for id in self.sources.drain(..) {
            id.remove();
        }
        *self.client.borrow_mut() = None;
        let _ = std::fs::remove_file(&self.path);
    }
}
