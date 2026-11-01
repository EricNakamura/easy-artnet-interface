use std::{
    collections::HashMap,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, SystemTime, UNIX_EPOCH},
};

use reqwest::redirect::Policy;
use tokio::sync::RwLock;

use crate::model::Device;

pub type Registry = Arc<RwLock<HashMap<String, Device>>>;

#[derive(Clone)]
pub struct AppState {
    pub devices: Registry,
    pub http: reqwest::Client,
    pub scanning: Arc<AtomicBool>,
}

impl AppState {
    pub fn new() -> Self {
        let http = reqwest::Client::builder()
            .timeout(Duration::from_secs(10))
            .redirect(Policy::none())
            .build()
            .expect("build HTTP client");

        Self {
            devices: Arc::new(RwLock::new(HashMap::new())),
            http,
            scanning: Arc::new(AtomicBool::new(false)),
        }
    }

    pub async fn upsert(&self, device: Device) {
        if device.id.is_empty() || device.ip.is_empty() || device.ip == "0.0.0.0" {
            return;
        }

        let mut devices = self.devices.write().await;
        let entry = devices
            .entry(device.id.clone())
            .or_insert_with(|| device.clone());

        if !device.ip.is_empty() {
            entry.ip = device.ip;
        }
        if device.port != 0 {
            entry.port = device.port;
        }
        if !device.name.is_empty() {
            entry.name = device.name;
        }
        if device.mac.is_some() {
            entry.mac = device.mac;
        }
        if device.fw.is_some() {
            entry.fw = device.fw;
        }
        entry.claimed |= device.claimed;
        entry.last_seen = device.last_seen.max(entry.last_seen);
        for source in device.sources {
            if !entry.sources.contains(&source) {
                entry.sources.push(source);
            }
        }
    }

    pub async fn get(&self, id: &str) -> Option<Device> {
        self.devices.read().await.get(id).cloned()
    }

    pub async fn list(&self) -> Vec<Device> {
        let mut devices: Vec<_> = self.devices.read().await.values().cloned().collect();
        devices.sort_by(|a, b| a.name.cmp(&b.name).then(a.id.cmp(&b.id)));
        devices
    }

    pub fn begin_scan(&self) -> bool {
        !self.scanning.swap(true, Ordering::SeqCst)
    }

    pub fn end_scan(&self) {
        self.scanning.store(false, Ordering::SeqCst);
    }
}

pub fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}
