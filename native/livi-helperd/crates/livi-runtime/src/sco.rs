// SCO audio bridge: accepts the phone's SCO connection (CVSD = raw PCM s16le 8kHz)
// and pipes it full-duplex to LIVI over /tmp/aa-sco.sock. One frame in, one frame out
// keeps the air pacing.

pub const SOCK_PATH: &str = "/tmp/aa-sco.sock";

#[cfg(target_os = "linux")]
pub fn serve(events: crate::livi_sock::Broadcaster) {
    std::thread::spawn(move || linux::run(events));
}

#[cfg(not(target_os = "linux"))]
pub fn serve(_events: crate::livi_sock::Broadcaster) {}

#[cfg(target_os = "linux")]
mod linux {
    use super::SOCK_PATH;
    use std::io::{Read, Write};
    use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
    use std::os::unix::net::{UnixListener, UnixStream};

    const BTPROTO_SCO: libc::c_int = 2;
    const SOL_SCO: libc::c_int = 17;
    const SCO_OPTIONS: libc::c_int = 1;

    #[repr(C)]
    struct SockaddrSco {
        sco_family: libc::sa_family_t,
        sco_bdaddr: [u8; 6],
    }

    #[repr(C)]
    struct ScoOptions {
        mtu: u16,
    }

    pub fn run(events: crate::livi_sock::Broadcaster) {
        let _ = std::fs::remove_file(SOCK_PATH);
        let unix = match UnixListener::bind(SOCK_PATH) {
            Ok(l) => l,
            Err(e) => {
                eprintln!("[sco] unix socket failed: {e}");
                return;
            }
        };
        let _ = std::fs::set_permissions(
            SOCK_PATH,
            std::os::unix::fs::PermissionsExt::from_mode(0o666),
        );
        unix.set_nonblocking(true).ok();

        let listen = match sco_listen() {
            Ok(fd) => fd,
            Err(e) => {
                eprintln!("[sco] listen failed: {e}");
                return;
            }
        };
        println!("[sco] listening (SCO + {SOCK_PATH})");

        let mut client: Option<UnixStream> = None;
        loop {
            let sco = match sco_accept(&listen) {
                Ok(pair) => pair,
                Err(e) => {
                    eprintln!("[sco] accept failed: {e}");
                    std::thread::sleep(std::time::Duration::from_secs(1));
                    continue;
                }
            };
            let (sco_fd, mtu) = sco;
            println!("[sco] audio connected (mtu {mtu})");
            events.push_json(format!("{{\"event\":\"sco\",\"up\":true,\"mtu\":{mtu}}}"));
            bridge(&sco_fd, mtu as usize, &unix, &mut client);
            println!("[sco] audio closed");
            events.push_json("{\"event\":\"sco\",\"up\":false}".to_string());
        }
    }

    /// One frame down, one frame up per cycle — the SCO read paces the uplink.
    fn bridge(
        sco: &OwnedFd,
        mtu: usize,
        unix: &UnixListener,
        client: &mut Option<UnixStream>,
    ) {
        let mut down = vec![0u8; mtu.max(48)];
        let mut up = vec![0u8; mtu.max(48)];
        loop {
            if let Ok((s, _)) = unix.accept() {
                s.set_nonblocking(true).ok();
                println!("[sco] LIVI attached");
                *client = Some(s);
            }
            let n = unsafe { libc::read(sco.as_raw_fd(), down.as_mut_ptr().cast(), down.len()) };
            if n <= 0 {
                return;
            }
            let n = n as usize;
            let mut drop_client = false;
            if let Some(c) = client.as_mut() {
                if c.write_all(&down[..n]).is_err() {
                    drop_client = true;
                }
                up[..n].fill(0);
                match c.read(&mut up[..n]) {
                    Ok(0) => drop_client = true,
                    Ok(_) => {}
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                    Err(_) => drop_client = true,
                }
            } else {
                up[..n].fill(0);
            }
            if drop_client {
                println!("[sco] LIVI detached");
                *client = None;
                up[..n].fill(0);
            }
            let _ = unsafe { libc::write(sco.as_raw_fd(), up.as_ptr().cast(), n) };
        }
    }

    fn sco_listen() -> std::io::Result<OwnedFd> {
        let raw = unsafe { libc::socket(libc::AF_BLUETOOTH, libc::SOCK_SEQPACKET, BTPROTO_SCO) };
        if raw < 0 {
            return Err(std::io::Error::last_os_error());
        }
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        let addr = SockaddrSco {
            sco_family: libc::AF_BLUETOOTH as libc::sa_family_t,
            sco_bdaddr: [0; 6], // BDADDR_ANY
        };
        let rc = unsafe {
            libc::bind(
                fd.as_raw_fd(),
                std::ptr::addr_of!(addr).cast(),
                std::mem::size_of::<SockaddrSco>() as libc::socklen_t,
            )
        };
        if rc != 0 {
            return Err(std::io::Error::last_os_error());
        }
        if unsafe { libc::listen(fd.as_raw_fd(), 1) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(fd)
    }

    fn sco_accept(listen: &OwnedFd) -> std::io::Result<(OwnedFd, u16)> {
        let raw = unsafe { libc::accept(listen.as_raw_fd(), std::ptr::null_mut(), std::ptr::null_mut()) };
        if raw < 0 {
            return Err(std::io::Error::last_os_error());
        }
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        let mut opts = ScoOptions { mtu: 48 };
        let mut len = std::mem::size_of::<ScoOptions>() as libc::socklen_t;
        unsafe {
            libc::getsockopt(
                fd.as_raw_fd(),
                SOL_SCO,
                SCO_OPTIONS,
                std::ptr::addr_of_mut!(opts).cast(),
                &mut len,
            );
        }
        Ok((fd, opts.mtu.max(24)))
    }
}
