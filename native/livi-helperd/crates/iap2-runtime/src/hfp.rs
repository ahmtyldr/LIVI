// HFP Hands-Free: SLC over RFCOMM so the phone treats the head unit as a car kit.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

// Bit 2 CLI, bit 3 voice recognition, bit 4 remote volume, bit 7 codec negotiation.
pub const HF_FEATURES: u32 = (1 << 2) | (1 << 3) | (1 << 4) | (1 << 7);

const OK: &str = "\r\nOK\r";

/// SLC engine: AG lines in, HF lines out. HF speaks first with AT+BRSF; the OK chain
/// walks BRSF → (BAC) → CIND=? → CIND? → CMER, after which the SLC stands.
#[derive(Default)]
pub struct Slc {
    ag_features: u32,
    sent_bac: bool,
    sent_cind_test: bool,
    sent_cind_read: bool,
    sent_cmer: bool,
    established: bool,
}

impl Slc {
    pub fn established(&self) -> bool {
        self.established
    }

    /// The opening command when no probe has sent it yet.
    pub fn hello() -> String {
        format!("AT+BRSF={HF_FEATURES}\r")
    }

    pub fn on_line(&mut self, line: &str) -> Vec<String> {
        let line = line.trim();
        if line.is_empty() {
            return vec![];
        }
        println!("[hfp] << {line}");

        if let Some(v) = line.strip_prefix("+BRSF:") {
            self.ag_features = v.trim().parse().unwrap_or(self.ag_features);
            return vec![];
        }
        if line == "OK" {
            if self.established {
                return vec![];
            }
            let both_codec_neg =
                HF_FEATURES & (1 << 7) != 0 && self.ag_features & (1 << 9) != 0;
            if self.ag_features > 0 && both_codec_neg && !self.sent_bac {
                self.sent_bac = true;
                return vec!["AT+BAC=1,2\r".into()];
            }
            if self.ag_features > 0 && !self.sent_cind_test {
                self.sent_cind_test = true;
                return vec!["AT+CIND=?\r".into()];
            }
            if self.sent_cind_test && !self.sent_cind_read {
                self.sent_cind_read = true;
                return vec!["AT+CIND?\r".into()];
            }
            if self.sent_cind_read && !self.sent_cmer {
                self.sent_cmer = true;
                return vec!["AT+CMER=3,0,0,1\r".into()];
            }
            if self.sent_cmer {
                self.established = true;
                println!("[hfp] SLC established");
            }
            return vec![];
        }
        if line.starts_with("+CIND:") || line == "ERROR" {
            return vec![];
        }

        if let Some(v) = line.strip_prefix("AT+BRSF=") {
            self.ag_features = v.trim().parse().unwrap_or(self.ag_features);
            return vec![format!("\r\n+BRSF: {HF_FEATURES}"), OK.into()];
        }
        if line == "AT+CIND=?" {
            return vec![
                "\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))".into(),
                OK.into(),
            ];
        }
        if line == "AT+CIND?" {
            return vec!["\r\n+CIND: 1,0,0,0,5,0,5".into(), OK.into()];
        }
        if line.starts_with("AT+CHLD=?") {
            return vec!["\r\n+CHLD: (0,1,2,3)".into(), OK.into()];
        }
        if line.starts_with("AT+BIND=?") {
            return vec!["\r\n+BIND: (1,2)".into(), OK.into()];
        }
        if line.starts_with("AT+BIND?") {
            return vec!["\r\n+BIND: 1,1".into(), "\r\n+BIND: 2,1".into(), OK.into()];
        }
        if let Some(v) = line.strip_prefix("+BCS:") {
            return vec![format!("AT+BCS={}\r", v.trim())];
        }
        if line.starts_with("AT+COPS") {
            if line.contains("=?") {
                return vec![OK.into()];
            }
            if line.contains('?') {
                return vec!["\r\n+COPS: 0,0,\"Carrier\"".into(), OK.into()];
            }
            return vec![OK.into()];
        }
        if line.starts_with("+CIEV:") || line == "RING" || line.starts_with("+CLIP:") {
            return vec![];
        }
        // Everything else the phone asks gets acknowledged: AT+CMER=, AT+BIND=, AT+BAC=,
        // ATA, AT+CHUP, ATD…, AT+BVRA=, AT+VGS=, AT+VGM=, AT+NREC=, AT+BTRH?, AT+CLIP=,
        // AT+CCWA=, AT+CMEE=, AT+CLCC, AT+CNUM, unknown.
        vec![OK.into()]
    }
}

/// Trigger + cooldown + per-phone channel cache around the raw RFCOMM prober.
#[derive(Clone, Default)]
pub struct Hfp {
    inner: Arc<HfpInner>,
}

#[derive(Default)]
#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
struct HfpInner {
    last_attempt: Mutex<Option<std::time::Instant>>,
    cache: Mutex<std::collections::HashMap<String, u8>>,
    established: AtomicBool,
}

impl Hfp {
    pub fn established(&self) -> bool {
        self.inner.established.load(Ordering::SeqCst)
    }

    #[cfg(target_os = "linux")]
    pub fn trigger(&self, mac: &str) {
        const COOLDOWN: std::time::Duration = std::time::Duration::from_secs(8);
        {
            let mut last = self.inner.last_attempt.lock().unwrap();
            if last.is_some_and(|t| t.elapsed() < COOLDOWN) {
                return;
            }
            *last = Some(std::time::Instant::now());
        }
        let inner = self.inner.clone();
        let mac = mac.to_string();
        tokio::task::spawn_blocking(move || linux::probe(&inner, &mac));
    }

    /// Incoming Profile1 connection: the AG connected to us, run the SLC on its fd.
    #[cfg(target_os = "linux")]
    pub fn accept(&self, fd: std::os::fd::OwnedFd) {
        let inner = self.inner.clone();
        tokio::task::spawn_blocking(move || linux::slc_loop(&inner, fd, Vec::new(), true));
    }
}

#[cfg(target_os = "linux")]
mod linux {
    use super::{HfpInner, Slc, HF_FEATURES};
    use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
    use std::sync::atomic::Ordering;
    use std::time::Duration;

    const BTPROTO_RFCOMM: libc::c_int = 3;
    const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
    const PROBE_TIMEOUT: Duration = Duration::from_secs(3);
    const AT_TIMEOUT: Duration = Duration::from_secs(300);

    #[repr(C)]
    struct SockaddrRc {
        rc_family: libc::sa_family_t,
        rc_bdaddr: [u8; 6],
        rc_channel: u8,
    }

    pub fn probe(inner: &HfpInner, mac: &str) {
        let cached = inner.cache.lock().unwrap().get(mac).copied();
        let mut candidates: Vec<u8> = Vec::new();
        candidates.extend(cached);
        candidates.extend((1..16).filter(|c| Some(*c) != cached));
        println!("[hfp] probing {} channels for HFP AG on {mac}", candidates.len());

        for ch in candidates {
            let fd = match rfcomm_connect(mac, ch) {
                Ok(fd) => fd,
                Err(e) if e.raw_os_error() == Some(libc::ECONNREFUSED) => continue,
                Err(e) if e.raw_os_error() == Some(libc::EBUSY) && Some(ch) == cached => {
                    println!("[hfp] ch={ch} busy (SLC already active)");
                    return;
                }
                Err(e) => {
                    println!("[hfp] ch={ch} error: {e}");
                    continue;
                }
            };

            let probe = format!("AT+BRSF={HF_FEATURES}\r");
            if write_all(&fd, probe.as_bytes()).is_err() || !readable(&fd, PROBE_TIMEOUT) {
                continue;
            }
            let mut buf = [0u8; 1024];
            let n = unsafe { libc::read(fd.as_raw_fd(), buf.as_mut_ptr().cast(), buf.len()) };
            if n <= 0 {
                continue;
            }
            let initial = buf[..n as usize].to_vec();
            if !initial.windows(5).any(|w| w == b"+BRSF") {
                continue;
            }

            inner.cache.lock().unwrap().insert(mac.to_string(), ch);
            println!("[hfp] ch={ch} HFP AG confirmed, starting SLC");
            slc_loop(inner, fd, initial, false);
            return;
        }
        println!("[hfp] channels 1-15 exhausted for {mac} — HFP AG not found");
    }

    /// Blocking AT loop until the peer hangs up or stays silent past AT_TIMEOUT.
    pub fn slc_loop(inner: &HfpInner, fd: OwnedFd, initial: Vec<u8>, send_hello: bool) {
        let mut slc = Slc::default();
        if send_hello && write_all(&fd, Slc::hello().as_bytes()).is_err() {
            return;
        }
        let mut buf = initial;
        loop {
            while let Some(pos) = buf.iter().position(|b| *b == b'\r') {
                let line: Vec<u8> = buf.drain(..=pos).collect();
                let line = String::from_utf8_lossy(&line[..line.len() - 1]).to_string();
                for out in slc.on_line(&line) {
                    if write_all(&fd, out.as_bytes()).is_err() {
                        return finish(inner);
                    }
                }
                if slc.established() {
                    inner.established.store(true, Ordering::SeqCst);
                }
            }
            if !readable(&fd, AT_TIMEOUT) {
                println!("[hfp] AT timeout — disconnecting");
                return finish(inner);
            }
            let mut chunk = [0u8; 1024];
            let n = unsafe { libc::read(fd.as_raw_fd(), chunk.as_mut_ptr().cast(), chunk.len()) };
            if n <= 0 {
                println!("[hfp] disconnected");
                return finish(inner);
            }
            buf.extend_from_slice(&chunk[..n as usize]);
        }
    }

    fn finish(inner: &HfpInner) {
        inner.established.store(false, Ordering::SeqCst);
    }

    fn rfcomm_connect(mac: &str, channel: u8) -> std::io::Result<OwnedFd> {
        let mut bdaddr = [0u8; 6];
        for (i, part) in mac.split(':').enumerate().take(6) {
            // sockaddr_rc carries the address little-endian, reversed from the string.
            bdaddr[5 - i] = u8::from_str_radix(part, 16).unwrap_or(0);
        }
        let raw = unsafe { libc::socket(libc::AF_BLUETOOTH, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, BTPROTO_RFCOMM) };
        if raw < 0 {
            return Err(std::io::Error::last_os_error());
        }
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        let addr = SockaddrRc { rc_family: libc::AF_BLUETOOTH as libc::sa_family_t, rc_bdaddr: bdaddr, rc_channel: channel };
        let rc = unsafe {
            libc::connect(
                fd.as_raw_fd(),
                std::ptr::addr_of!(addr).cast(),
                std::mem::size_of::<SockaddrRc>() as libc::socklen_t,
            )
        };
        if rc != 0 {
            let err = std::io::Error::last_os_error();
            if err.raw_os_error() != Some(libc::EINPROGRESS) {
                return Err(err);
            }
            if !writable(&fd, CONNECT_TIMEOUT) {
                return Err(std::io::ErrorKind::TimedOut.into());
            }
            let mut so_err: libc::c_int = 0;
            let mut len = std::mem::size_of::<libc::c_int>() as libc::socklen_t;
            unsafe {
                libc::getsockopt(
                    fd.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_ERROR,
                    std::ptr::addr_of_mut!(so_err).cast(),
                    &mut len,
                );
            }
            if so_err != 0 {
                return Err(std::io::Error::from_raw_os_error(so_err));
            }
        }
        // Back to blocking for the AT loop.
        unsafe {
            let fl = libc::fcntl(fd.as_raw_fd(), libc::F_GETFL);
            libc::fcntl(fd.as_raw_fd(), libc::F_SETFL, fl & !libc::O_NONBLOCK);
        }
        Ok(fd)
    }

    fn poll(fd: &OwnedFd, events: libc::c_short, timeout: Duration) -> bool {
        let mut pfd = libc::pollfd { fd: fd.as_raw_fd(), events, revents: 0 };
        let rc = unsafe { libc::poll(&mut pfd, 1, timeout.as_millis() as libc::c_int) };
        rc > 0 && pfd.revents & events != 0
    }

    fn readable(fd: &OwnedFd, timeout: Duration) -> bool {
        poll(fd, libc::POLLIN, timeout)
    }

    fn writable(fd: &OwnedFd, timeout: Duration) -> bool {
        poll(fd, libc::POLLOUT, timeout)
    }

    fn write_all(fd: &OwnedFd, mut data: &[u8]) -> std::io::Result<()> {
        while !data.is_empty() {
            let n = unsafe { libc::write(fd.as_raw_fd(), data.as_ptr().cast(), data.len()) };
            if n <= 0 {
                return Err(std::io::Error::last_os_error());
            }
            data = &data[n as usize..];
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn drive(slc: &mut Slc, line: &str) -> Vec<String> {
        slc.on_line(line)
    }

    #[test]
    fn slc_with_codec_negotiation() {
        let mut slc = Slc::default();
        assert_eq!(Slc::hello(), format!("AT+BRSF={HF_FEATURES}\r"));
        assert!(drive(&mut slc, "+BRSF:4095").is_empty());
        assert_eq!(drive(&mut slc, "OK"), vec!["AT+BAC=1,2\r"]);
        assert_eq!(drive(&mut slc, "OK"), vec!["AT+CIND=?\r"]);
        assert!(drive(&mut slc, "+CIND: (\"service\",(0,1))").is_empty());
        assert_eq!(drive(&mut slc, "OK"), vec!["AT+CIND?\r"]);
        assert!(drive(&mut slc, "+CIND: 1,0,0,0,5,0,5").is_empty());
        assert_eq!(drive(&mut slc, "OK"), vec!["AT+CMER=3,0,0,1\r"]);
        assert!(!slc.established());
        assert!(drive(&mut slc, "OK").is_empty());
        assert!(slc.established());
    }

    #[test]
    fn slc_without_codec_negotiation() {
        let mut slc = Slc::default();
        drive(&mut slc, "+BRSF:256");
        assert_eq!(drive(&mut slc, "OK"), vec!["AT+CIND=?\r"]);
    }

    #[test]
    fn answers_ag_initiated_commands() {
        let mut slc = Slc::default();
        let out = drive(&mut slc, "AT+BRSF=511");
        assert_eq!(out, vec![format!("\r\n+BRSF: {HF_FEATURES}"), "\r\nOK\r".to_string()]);
        assert_eq!(drive(&mut slc, "AT+CIND?"), vec!["\r\n+CIND: 1,0,0,0,5,0,5", "\r\nOK\r"]);
        assert_eq!(drive(&mut slc, "+BCS:2"), vec!["AT+BCS=2\r"]);
        assert_eq!(drive(&mut slc, "AT+COPS?"), vec!["\r\n+COPS: 0,0,\"Carrier\"", "\r\nOK\r"]);
        assert_eq!(drive(&mut slc, "AT+WEIRD"), vec!["\r\nOK\r"]);
        assert!(drive(&mut slc, "RING").is_empty());
    }
}
