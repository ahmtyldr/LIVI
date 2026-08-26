// CarPlay mDNS: publishes the AirPlay receiver and finds the phone's control endpoint.

use std::io::{Read, Write};
use std::net::{Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6};
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use socket2::{Domain, Protocol, Socket, Type};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

use crate::livi_sock::Broadcaster;

const AIRPLAY_SERVICE: &str = "_airplay._tcp";
const CARPLAY_CTRL: &str = "_carplay-ctrl._tcp";

pub struct Bonjour {
    _publisher: Child,
    device_id: String,
    source_version: String,
    bcast: Broadcaster,
    seen: Arc<Mutex<std::collections::HashMap<String, (String, u16)>>>,
}

fn txt_records(device_id: &str, source_version: &str, pk: &str, pi: &str) -> Vec<String> {
    let mut txt = vec![
        format!("deviceid={device_id}"),
        "features=0x44540380,0x61".to_string(),
        "flags=0x4".to_string(),
        "model=LIVI".to_string(),
        format!("srcvers={source_version}"),
        "protovers=1.1".to_string(),
    ];
    if !pi.is_empty() {
        txt.push(format!("pi={pi}"));
    }
    if !pk.is_empty() {
        txt.push(format!("pk={pk}"));
    }
    txt
}

impl Bonjour {
    pub fn start(
        device_id: String,
        airplay_port: u16,
        source_version: String,
        pk: String,
        pi: String,
        bcast: Broadcaster,
    ) -> std::io::Result<Self> {
        let txt = txt_records(&device_id, &source_version, &pk, &pi);
        let mut args = vec![
            "LIVI".to_string(),
            AIRPLAY_SERVICE.to_string(),
            airplay_port.to_string(),
        ];
        args.extend(txt);
        let publisher = Command::new("avahi-publish-service")
            .args(&args)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()?;
        println!("[cp] avahi published {AIRPLAY_SERVICE} port={airplay_port}");

        let bonjour = Self {
            _publisher: publisher,
            device_id,
            source_version,
            bcast,
            seen: Arc::new(Mutex::new(std::collections::HashMap::new())),
        };
        bonjour.spawn_browser();
        Ok(bonjour)
    }

    fn spawn_browser(&self) {
        let device_id = self.device_id.clone();
        let source_version = self.source_version.clone();
        let bcast = self.bcast.clone();
        let seen = self.seen.clone();
        tokio::spawn(async move {
            loop {
                if let Err(e) = browse_once(&device_id, &source_version, &bcast, &seen).await {
                    eprintln!("[cp] carplay-ctrl browse ended: {e}");
                }
                tokio::time::sleep(Duration::from_secs(2)).await;
            }
        });
    }
}

async fn browse_once(
    device_id: &str,
    source_version: &str,
    bcast: &Broadcaster,
    seen: &Arc<Mutex<std::collections::HashMap<String, (String, u16)>>>,
) -> std::io::Result<()> {
    let mut child = Command::new("avahi-browse")
        .args(["-r", "-p", "-k", CARPLAY_CTRL])
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()?;
    let stdout = child.stdout.take().expect("piped stdout");
    let mut lines = BufReader::new(stdout).lines();
    while let Some(line) = lines.next_line().await? {
        if !line.starts_with('=') {
            continue;
        }
        let Some(ep) = parse_resolved(&line) else { continue };
        let key = ep.phone_bt.clone().unwrap_or_else(|| ep.address.clone());
        let endpoint = (ep.address.clone(), ep.port);
        let fresh = {
            let mut s = seen.lock().unwrap();
            if s.get(&key) == Some(&endpoint) {
                false
            } else {
                s.insert(key.clone(), endpoint.clone());
                true
            }
        };
        if !fresh {
            continue;
        }
        println!("[cp] found phone {CARPLAY_CTRL} at {}:{}", ep.address, ep.port);
        if let Some(mac) = &ep.phone_bt {
            let ip = ep.address.split('%').next().unwrap_or(&ep.address);
            bcast.push_json(format!(
                "{{\"type\":\"device\",\"src\":\"bonjour\",\"btMac\":\"{mac}\",\"ip\":\"{ip}\"}}"
            ));
        }
        let device_id = device_id.to_string();
        let source_version = source_version.to_string();
        tokio::task::spawn_blocking(move || connect_probe(&ep, &device_id, &source_version));
    }
    Ok(())
}

struct Endpoint {
    #[cfg_attr(not(target_os = "linux"), allow(dead_code))]
    iface: String,
    address: String,
    port: u16,
    phone_bt: Option<String>,
}

fn parse_resolved(line: &str) -> Option<Endpoint> {
    let f: Vec<&str> = line.split(';').collect();
    if f.len() < 10 {
        return None;
    }
    let iface = f[1].to_string();
    let proto = f[2];
    let mut address = f[7].to_string();
    let port: u16 = f[8].parse().ok()?;
    let txt = f[9];

    let is_v6 = proto == "IPv6" || address.contains(':');
    if is_v6 {
        if !address.to_lowercase().starts_with("fe80") {
            return None;
        }
        if !iface.is_empty() && !address.contains('%') {
            address = format!("{address}%{iface}");
        }
    } else if address.starts_with("169.254.") {
        return None;
    }

    let phone_bt = txt
        .split(&['"', ' '][..])
        .find_map(|t| t.strip_prefix("id="))
        .map(|s| s.trim().to_lowercase());

    Some(Endpoint { iface, address, port, phone_bt })
}

fn connect_probe(ep: &Endpoint, device_id: &str, source_version: &str) {
    let mac_int = device_id.replace(':', "");
    let host = ep.address.split('%').next().unwrap_or(&ep.address);
    let is_v6 = ep.address.contains(':');
    let host_hdr = if is_v6 { format!("[{host}]:{}", ep.port) } else { format!("{host}:{}", ep.port) };
    let req = format!(
        "GET /ctrl-int/1/connect HTTP/1.1\r\nHost: {host_hdr}\r\nUser-Agent: AirPlay/{source_version}\r\nAirPlay-Receiver-Device-ID: {mac_int}\r\nConnection: close\r\n\r\n"
    );

    for attempt in 1..=7 {
        match probe_attempt(ep, host, is_v6, req.as_bytes()) {
            Ok(first) => {
                println!("[cp] /ctrl-int/1/connect -> {first:?} (attempt {attempt})");
                return;
            }
            Err(e) => {
                if attempt == 7 {
                    println!("[cp] /ctrl-int/1/connect gave up: {e}");
                }
                std::thread::sleep(Duration::from_millis(1500));
            }
        }
    }
}

fn probe_attempt(ep: &Endpoint, host: &str, is_v6: bool, req: &[u8]) -> std::io::Result<String> {
    let (domain, addr): (Domain, SocketAddr) = if is_v6 {
        let ip: Ipv6Addr = host.parse().map_err(|_| io_err("bad v6"))?;
        let scope = ep.address.split('%').nth(1).and_then(nametoindex).unwrap_or(0);
        (Domain::IPV6, SocketAddr::V6(SocketAddrV6::new(ip, ep.port, 0, scope)))
    } else {
        let ip: Ipv4Addr = host.parse().map_err(|_| io_err("bad v4"))?;
        (Domain::IPV4, SocketAddr::V4(SocketAddrV4::new(ip, ep.port)))
    };

    let sock = Socket::new(domain, Type::STREAM, Some(Protocol::TCP))?;
    #[cfg(target_os = "linux")]
    if !ep.iface.is_empty() {
        let _ = sock.bind_device(Some(ep.iface.as_bytes()));
    }
    sock.set_read_timeout(Some(Duration::from_secs(3)))?;
    sock.set_write_timeout(Some(Duration::from_secs(3)))?;
    sock.connect_timeout(&addr.into(), Duration::from_secs(3))?;

    let mut stream: std::net::TcpStream = sock.into();
    stream.write_all(req)?;
    let mut buf = [0u8; 256];
    let n = stream.read(&mut buf)?;
    if n == 0 {
        return Err(io_err("empty response"));
    }
    let text = String::from_utf8_lossy(&buf[..n]);
    Ok(text.lines().next().unwrap_or("").to_string())
}

fn nametoindex(name: &str) -> Option<u32> {
    let cname = std::ffi::CString::new(name).ok()?;
    let idx = unsafe { libc::if_nametoindex(cname.as_ptr()) };
    if idx == 0 {
        None
    } else {
        Some(idx)
    }
}

fn io_err(msg: &str) -> std::io::Error {
    std::io::Error::other(msg)
}
