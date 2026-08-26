use std::process::Command;

pub fn wlan_mac(iface: &str) -> Option<String> {
    std::fs::read_to_string(format!("/sys/class/net/{iface}/address"))
        .ok()
        .map(|s| s.trim().to_uppercase())
        .filter(|s| !s.is_empty())
}

pub fn wlan_link_local(iface: &str) -> Option<String> {
    let out = Command::new("ip")
        .args(["-6", "-o", "addr", "show", "dev", iface, "scope", "link"])
        .output()
        .ok()?;
    let text = String::from_utf8_lossy(&out.stdout);
    text.split_whitespace()
        .find(|t| t.starts_with("fe80:"))
        .map(|t| t.split('/').next().unwrap_or(t).to_string())
}

pub fn ap_ssid_channel(iface: &str) -> (Option<String>, Option<u8>) {
    let Ok(out) = Command::new("iw").args(["dev", iface, "info"]).output() else {
        return (None, None);
    };
    let text = String::from_utf8_lossy(&out.stdout);
    let mut ssid = None;
    let mut channel = None;
    for line in text.lines() {
        let line = line.trim();
        if let Some(v) = line.strip_prefix("ssid ") {
            ssid = Some(v.trim().to_string());
        } else if let Some(v) = line.strip_prefix("channel ") {
            channel = v.split_whitespace().next().and_then(|c| c.parse().ok());
        }
    }
    (ssid, channel)
}
