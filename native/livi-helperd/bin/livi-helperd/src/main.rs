use std::process::ExitCode;

#[cfg(target_os = "linux")]
mod linux_main;
#[cfg(target_os = "linux")]
mod wired;
#[cfg(target_os = "linux")]
mod aa;

fn main() -> ExitCode {
    #[cfg(target_os = "linux")]
    {
        if std::env::args().any(|a| a == "--wifi-ap") {
            return linux_main::run_wifi_ap();
        }
        linux_main::run()
    }
    #[cfg(not(target_os = "linux"))]
    {
        eprintln!("cp-bringup needs BlueZ and i2c; build and run it on the Pi");
        ExitCode::FAILURE
    }
}
