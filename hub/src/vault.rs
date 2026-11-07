use std::{
    collections::HashMap,
    env,
    sync::{Arc, Mutex},
};

use crate::auth::normalize_code;

#[derive(Clone)]
pub struct Credentials {
    pub code: String,
    pub key: [u8; 32],
    pub counter: u64,
}

#[derive(Clone)]
pub struct AuthStore {
    inner: Arc<Mutex<HashMap<String, Credentials>>>,
    use_keyring: bool,
}

impl AuthStore {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(HashMap::new())),
            use_keyring: env::var_os("EASYARTNET_MEMORY_VAULT").is_none(),
        }
    }

    #[cfg(test)]
    pub fn memory() -> Self {
        Self {
            inner: Arc::new(Mutex::new(HashMap::new())),
            use_keyring: false,
        }
    }

    pub fn save(&self, id: &str, code: &str, key: &[u8; 32]) {
        let code = normalize_code(code);
        {
            let mut inner = self.inner.lock().expect("vault lock");
            inner.insert(
                id.to_string(),
                Credentials {
                    code: code.clone(),
                    key: *key,
                    counter: 0,
                },
            );
        }

        if self.use_keyring {
            if let Some(entry) = Self::entry(id, "code") {
                let _ = entry.set_password(&code);
            }
            if let Some(entry) = Self::entry(id, "key") {
                let _ = entry.set_password(&hex::encode(key));
            }
        }
    }

    pub fn load(&self, id: &str) -> Option<Credentials> {
        if let Some(credentials) = self.inner.lock().expect("vault lock").get(id).cloned() {
            return Some(credentials);
        }

        if !self.use_keyring {
            return None;
        }

        let code = Self::entry(id, "code")?.get_password().ok()?;
        let key_hex = Self::entry(id, "key")?.get_password().ok()?;
        let key: [u8; 32] = hex::decode(key_hex).ok()?.try_into().ok()?;
        let credentials = Credentials {
            code: normalize_code(&code),
            key,
            counter: 0,
        };
        self.inner
            .lock()
            .expect("vault lock")
            .insert(id.to_string(), credentials.clone());
        Some(credentials)
    }

    pub fn next_counter(&self, id: &str) -> Option<u64> {
        let mut inner = self.inner.lock().expect("vault lock");
        let credentials = inner.get_mut(id)?;
        credentials.counter = credentials.counter.saturating_add(1);
        Some(credentials.counter)
    }

    fn entry(id: &str, suffix: &str) -> Option<keyring::Entry> {
        keyring::Entry::new("easy-artnet-hub", &format!("{id}:{suffix}")).ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn memory_vault_round_trip() {
        let vault = AuthStore::memory();
        let key = [7u8; 32];
        vault.save("dmx-test", "abcdefghijklmnop", &key);
        let loaded = vault.load("dmx-test").unwrap();
        assert_eq!(loaded.code, "ABCDEFGHIJKLMNOP");
        assert_eq!(loaded.key, key);
        assert_eq!(vault.next_counter("dmx-test"), Some(1));
        assert_eq!(vault.next_counter("dmx-test"), Some(2));
    }
}
