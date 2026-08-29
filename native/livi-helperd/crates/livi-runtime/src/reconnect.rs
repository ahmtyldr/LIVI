// Pages known wireless phones back after a restart.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use zbus::Connection;

use crate::state::HelperState;

// Paging occupies the controller, so an absent phone backs off instead of holding the radio.
const INTERVAL: Duration = Duration::from_secs(5);
const BURST_ATTEMPTS: u32 = 3;
const BACKOFF_MAX: Duration = Duration::from_secs(60);
const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
const STALE: Duration = Duration::from_secs(10);
const STARTUP_WINDOW: Duration = Duration::from_secs(30);
const STARTUP_INTERVAL: Duration = Duration::from_secs(1);

pub async fn run(conn: Connection, adapter: String, state: Arc<HelperState>) {
    let started = Instant::now();
    let paging = Arc::new(AtomicBool::new(false));
    let mut stale_since: HashMap<String, Instant> = HashMap::new();
    let attempts: Arc<Mutex<HashMap<String, u32>>> = Arc::new(Mutex::new(HashMap::new()));
    let next_try: Arc<Mutex<HashMap<String, Instant>>> = Arc::new(Mutex::new(HashMap::new()));

    loop {
        let startup = started.elapsed() < STARTUP_WINDOW;
        tokio::time::sleep(if startup { STARTUP_INTERVAL } else { INTERVAL }).await;
        let targets = state.reconnect_targets();
        stale_since.retain(|mac, _| targets.iter().any(|(m, _)| m == mac));
        if targets.is_empty() {
            continue;
        }

        for (mac, uuid) in targets {
            let path = device_path(&adapter, &mac);
            let connected = device_connected(&conn, &path).await.unwrap_or(false);

            if !connected {
                stale_since.remove(&mac);
                if next_try.lock().unwrap().get(&mac).is_some_and(|t| Instant::now() < *t) {
                    continue;
                }
                if paging.swap(true, Ordering::SeqCst) {
                    continue; // one page at a time — the controller pages a single device
                }
                spawn_page(
                    conn.clone(),
                    path,
                    mac.clone(),
                    uuid.clone(),
                    paging.clone(),
                    attempts.clone(),
                    next_try.clone(),
                    "paging",
                );
                continue;
            }

            // Connected + no session: ConnectProfile on the existing ACL, Disconnect after
            // STALE. Backoff resets only on a successful page — Connected flickers during
            // failed ones.
            let stale = match stale_since.entry(mac.clone()) {
                Entry::Vacant(e) => {
                    e.insert(Instant::now());
                    false
                }
                Entry::Occupied(e) => e.get().elapsed() >= STALE && {
                    e.remove();
                    true
                },
            };
            if stale {
                println!("[cp] reconnect: {mac} connected but no session, disconnecting");
                let _ = disconnect(&conn, &path).await;
                continue;
            }
            if uuid.is_none() {
                continue; // plain Connect() is a no-op on a connected device
            }
            if next_try.lock().unwrap().get(&mac).is_some_and(|t| Instant::now() < *t) {
                continue;
            }
            if paging.swap(true, Ordering::SeqCst) {
                continue;
            }
            spawn_page(
                conn.clone(),
                path,
                mac.clone(),
                uuid.clone(),
                paging.clone(),
                attempts.clone(),
                next_try.clone(),
                "profile nudge",
            );
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn spawn_page(
    conn: Connection,
    path: String,
    mac: String,
    uuid: Option<String>,
    paging: Arc<AtomicBool>,
    attempts: Arc<Mutex<HashMap<String, u32>>>,
    next_try: Arc<Mutex<HashMap<String, Instant>>>,
    verb: &'static str,
) {
    tokio::spawn(async move {
        println!("[cp] reconnect: {verb} {mac}");
        let outcome = tokio::time::timeout(CONNECT_TIMEOUT, page(&conn, &path, uuid.as_deref())).await;
        let connected = matches!(outcome, Ok(Ok(())));
        match outcome {
            Ok(Ok(())) => {
                println!("[cp] reconnect: {mac} connected");
                attempts.lock().unwrap().remove(&mac);
                next_try.lock().unwrap().remove(&mac);
            }
            Ok(Err(e)) => println!("[cp] reconnect: {mac} page failed: {e}"),
            Err(_) => {
                println!("[cp] reconnect: {mac} page timed out, clearing stuck attempt");
                let _ = disconnect(&conn, &path).await;
            }
        }
        if !connected {
            let tries = {
                let mut a = attempts.lock().unwrap();
                let n = a.entry(mac.clone()).or_insert(0);
                *n += 1;
                *n
            };
            if tries > BURST_ATTEMPTS {
                let delay = backoff(tries);
                println!("[cp] reconnect: {mac} backing off {}s", delay.as_secs());
                next_try.lock().unwrap().insert(mac.clone(), Instant::now() + delay);
            }
        }
        paging.store(false, Ordering::SeqCst);
    });
}

/// Doubles from the scan interval up to the cap.
fn backoff(tries: u32) -> Duration {
    let steps = tries.saturating_sub(BURST_ATTEMPTS).min(6);
    BACKOFF_MAX.min(INTERVAL * 2u32.saturating_pow(steps))
}

fn device_path(adapter: &str, mac: &str) -> String {
    format!("/org/bluez/{}/dev_{}", adapter, mac.replace(':', "_").to_uppercase())
}

async fn device_connected(conn: &Connection, path: &str) -> Option<bool> {
    let reply = conn
        .call_method(
            Some("org.bluez"),
            path,
            Some("org.freedesktop.DBus.Properties"),
            "Get",
            &("org.bluez.Device1", "Connected"),
        )
        .await
        .ok()?;
    let value: zbus::zvariant::OwnedValue = reply.body().deserialize().ok()?;
    bool::try_from(value).ok()
}

async fn page(conn: &Connection, path: &str, uuid: Option<&str>) -> Result<(), zbus::Error> {
    match uuid {
        Some(uuid) => {
            conn.call_method(Some("org.bluez"), path, Some("org.bluez.Device1"), "ConnectProfile", &(uuid,))
                .await?;
        }
        None => {
            conn.call_method(Some("org.bluez"), path, Some("org.bluez.Device1"), "Connect", &())
                .await?;
        }
    }
    Ok(())
}

async fn disconnect(conn: &Connection, path: &str) -> Result<(), zbus::Error> {
    conn.call_method(Some("org.bluez"), path, Some("org.bluez.Device1"), "Disconnect", &()).await?;
    Ok(())
}
