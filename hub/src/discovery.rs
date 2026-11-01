use std::{
    collections::HashSet,
    net::{Ipv4Addr, SocketAddr},
    time::Duration,
};

use futures::stream::{self, StreamExt};
use if_addrs::IfAddr;
use ipnet::Ipv4Net;
use mdns_sd::{ServiceDaemon, ServiceEvent};
use tokio::net::UdpSocket;

use crate::{
    model::{Beacon, Device, DeviceInfo},
    state::{now_secs, AppState},
};

pub fn start_mdns(state: AppState) {
    tokio::spawn(async move {
        let daemon = match ServiceDaemon::new() {
            Ok(daemon) => daemon,
            Err(error) => {
                eprintln!("mDNS indisponível: {error}");
                return;
            }
        };
        let receiver = match daemon.browse("_easyartnet._tcp.local.") {
            Ok(receiver) => receiver,
            Err(error) => {
                eprintln!("mDNS browse falhou: {error}");
                return;
            }
        };
        println!("mDNS: à procura de _easyartnet._tcp.local.");

        let _keep_daemon = daemon;
        while let Ok(event) = receiver.recv_async().await {
            if let ServiceEvent::ServiceResolved(info) = event {
                let Some(ip) = info.get_addresses_v4().into_iter().next().copied() else {
                    continue;
                };
                let id = info
                    .get_property_val_str("id")
                    .map(str::to_string)
                    .unwrap_or_else(|| info.get_fullname().to_string());
                let port = info.get_port();
                let fw = info.get_property_val_str("fw").map(str::to_string);

                state
                    .upsert(Device {
                        id: id.clone(),
                        name: id,
                        mac: None,
                        ip: ip.to_string(),
                        port,
                        fw,
                        claimed: false,
                        last_seen: now_secs(),
                        sources: vec!["mdns".into()],
                    })
                    .await;

                let state = state.clone();
                tokio::spawn(async move {
                    refresh_info(&state, ip.to_string(), port).await;
                });
            }
        }
    });
}

pub fn start_beacon(state: AppState) {
    tokio::spawn(async move {
        let socket = match UdpSocket::bind(SocketAddr::from(([0, 0, 0, 0], 6456))).await {
            Ok(socket) => socket,
            Err(error) => {
                eprintln!("beacon UDP 6456 indisponível: {error}");
                return;
            }
        };
        println!("Beacon: à escuta em UDP 6456");

        let mut buffer = [0u8; 768];
        loop {
            let (len, _from) = match socket.recv_from(&mut buffer).await {
                Ok(packet) => packet,
                Err(error) => {
                    eprintln!("beacon recv: {error}");
                    continue;
                }
            };
            let Ok(beacon) = serde_json::from_slice::<Beacon>(&buffer[..len]) else {
                continue;
            };
            if beacon.ip == "0.0.0.0" || beacon.id.is_empty() {
                continue;
            }
            let id = beacon.id.clone();
            state
                .upsert(Device {
                    id: id.clone(),
                    name: beacon.name.unwrap_or(id),
                    mac: None,
                    ip: beacon.ip,
                    port: beacon.port.unwrap_or(80),
                    fw: beacon.fw,
                    claimed: beacon.claimed,
                    last_seen: now_secs(),
                    sources: vec!["beacon".into()],
                })
                .await;
        }
    });
}

pub fn spawn_scan(state: AppState) {
    if !state.begin_scan() {
        return;
    }
    tokio::spawn(async move {
        let found = scan(&state).await;
        state.end_scan();
        println!("scan: {found} dispositivo(s) encontrados");
    });
}

async fn scan(state: &AppState) -> usize {
    let targets = scan_targets();
    if targets.is_empty() {
        return 0;
    }

    let found: Vec<Device> = stream::iter(targets)
        .map(|ip| {
            let state = state.clone();
            async move {
                let ip = ip.to_string();
                fetch_info(&state.http, &ip, 80, Duration::from_millis(350))
                    .await
                    .map(|info| device_from_info(info, &ip, 80, "scan"))
            }
        })
        .buffer_unordered(32)
        .filter_map(|device| async move { device })
        .collect()
        .await;

    for device in &found {
        state.upsert(device.clone()).await;
    }
    found.len()
}

fn scan_targets() -> Vec<Ipv4Addr> {
    let mut targets = HashSet::new();
    let interfaces = if_addrs::get_if_addrs().unwrap_or_default();

    for interface in interfaces {
        let IfAddr::V4(v4) = interface.addr else {
            continue;
        };
        if v4.is_loopback() || v4.is_link_local() {
            continue;
        }

        // Nunca varrer mais de /24 por interface; /16 vira a /24 que contém o IP.
        let prefix = v4.prefixlen.max(24);
        let Ok(network) = Ipv4Net::new(v4.ip, prefix) else {
            continue;
        };
        for host in network.trunc().hosts().take(254) {
            if host != v4.ip {
                targets.insert(host);
            }
        }
    }

    targets.into_iter().collect()
}

pub async fn refresh_info(state: &AppState, ip: String, port: u16) {
    if let Some(info) = fetch_info(&state.http, &ip, port, Duration::from_secs(2)).await {
        state
            .upsert(device_from_info(info, &ip, port, "info"))
            .await;
    }
}

pub async fn fetch_info(
    http: &reqwest::Client,
    ip: &str,
    port: u16,
    timeout: Duration,
) -> Option<DeviceInfo> {
    let response = http
        .get(format!("http://{ip}:{port}/api/info"))
        .timeout(timeout)
        .send()
        .await
        .ok()?;
    if !response.status().is_success() {
        return None;
    }
    response.json::<DeviceInfo>().await.ok()
}

pub fn device_from_info(info: DeviceInfo, ip: &str, port: u16, source: &str) -> Device {
    let device_ip = info.ip.unwrap_or_else(|| ip.to_string());
    let id = info.id.unwrap_or_else(|| device_ip.clone());
    let name = info.name.unwrap_or_else(|| id.clone());
    Device {
        id,
        name,
        mac: info.mac,
        ip: device_ip,
        port: info.port.unwrap_or(port),
        fw: info.fw,
        claimed: info.claimed,
        last_seen: now_secs(),
        sources: vec![source.into()],
    }
}

#[cfg(test)]
mod tests {
    use super::scan_targets;

    #[test]
    fn scan_targets_are_unique_and_bounded() {
        let targets = scan_targets();
        assert!(targets.len() <= 254 * 4);
        assert_eq!(
            targets.len(),
            targets
                .iter()
                .collect::<std::collections::HashSet<_>>()
                .len()
        );
    }
}
