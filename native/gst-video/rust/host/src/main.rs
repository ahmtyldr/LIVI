//! Entry shell for livi-gst-host. On Linux the process entry is the C++ main()
//! from the whole-archive gst_video objects; elsewhere the binary is a stub.
#![cfg_attr(target_os = "linux", no_main)]

#[cfg(not(target_os = "linux"))]
fn main() {
    eprintln!("livi-gst-host runs on Linux only");
    std::process::exit(1);
}
