// USB access for CarPlay: selects the phone's CarPlay configuration without a role switch.

pub const APPLE_VID: u16 = 0x05ac;
pub const CP_CONFIG: u8 = 6;

pub mod ntb;

pub const EP_OUT: u8 = 0x04;
pub const EP_IN: u8 = 0x85;

#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "linux")]
pub use linux::{
    ensure_carplay_config, find_iphones, open_by_address, restore_all_default_config,
    restore_default_config, IPhoneDev,
};

#[cfg(target_os = "linux")]
mod mux;
#[cfg(target_os = "linux")]
pub use mux::{MuxHost, MuxTcpConn, LOCKDOWN_PORT};

#[cfg(target_os = "linux")]
mod device;
#[cfg(target_os = "linux")]
pub use device::{socket_path, MuxDevice, MuxRegistry};

#[cfg(target_os = "linux")]
mod ncm;
#[cfg(target_os = "linux")]
pub use ncm::NcmBridge;

#[cfg(target_os = "linux")]
mod async_stream;
#[cfg(target_os = "linux")]
pub use async_stream::AsyncMuxStream;
