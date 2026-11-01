use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Device {
    pub id: String,
    pub name: String,
    pub mac: Option<String>,
    pub ip: String,
    pub port: u16,
    pub fw: Option<String>,
    pub claimed: bool,
    pub last_seen: u64,
    pub sources: Vec<String>,
}

#[derive(Debug, Deserialize)]
pub struct DeviceInfo {
    #[serde(default)]
    pub id: Option<String>,
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub mac: Option<String>,
    #[serde(default)]
    pub ip: Option<String>,
    #[serde(default)]
    pub port: Option<u16>,
    #[serde(default)]
    pub fw: Option<String>,
    #[serde(default)]
    pub claimed: bool,
}

#[derive(Debug, Deserialize)]
pub struct Beacon {
    pub id: String,
    #[serde(default)]
    pub name: Option<String>,
    pub ip: String,
    #[serde(default)]
    pub port: Option<u16>,
    #[serde(default)]
    pub fw: Option<String>,
    #[serde(default)]
    pub claimed: bool,
}

#[derive(Debug, Deserialize)]
pub struct AddDevice {
    pub address: String,
}

#[derive(Debug, Deserialize)]
pub struct ClaimRequest {
    pub code: String,
}
