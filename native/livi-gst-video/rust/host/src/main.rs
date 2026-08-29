//! Entry shell for livi-gst-host. On Linux the process entry is the C++ main()
//! from the whole-archive gst_video objects; elsewhere the binary is a stub.
#![cfg_attr(target_os = "linux", no_main)]

// The screen receiver's AEAD symbols live in this crate; the reference keeps
// rustc from dropping the otherwise-unused dependency at link time.
#[cfg(target_os = "linux")]
use livi_crypto_node as _;

#[cfg(not(target_os = "linux"))]
fn main() {
    eprintln!("livi-gst-host runs on Linux only");
    std::process::exit(1);
}
