use hmac::{Hmac, Mac};
use percent_encoding::percent_decode_str;
use rand::{rngs::OsRng, RngCore};
use serde::Serialize;
use sha2::Sha256;

type HmacSha256 = Hmac<Sha256>;

#[derive(Clone, Debug, Serialize)]
pub struct ClaimPayload {
    pub nonce: String,
    pub cipher: String,
    pub tag: String,
}

pub fn normalize_code(code: &str) -> String {
    code.trim()
        .chars()
        .filter(|c| !matches!(c, '-' | ' ' | ':'))
        .map(|c| c.to_ascii_uppercase())
        .collect()
}

fn hmac(key: &[u8], data: &[u8]) -> [u8; 32] {
    let mut mac = HmacSha256::new_from_slice(key).expect("HMAC key");
    mac.update(data);
    let bytes = mac.finalize().into_bytes();
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

pub fn claim_key(code: &str) -> [u8; 32] {
    hmac(code.as_bytes(), b"easyartnet-claim")
}

pub fn make_claim(code: &str) -> (ClaimPayload, [u8; 32]) {
    let code = normalize_code(code);
    let key = claim_key(&code);

    let mut nonce = [0u8; 16];
    let mut session = [0u8; 32];
    OsRng.fill_bytes(&mut nonce);
    OsRng.fill_bytes(&mut session);

    let mut keystream_data = Vec::from(&b"keystream"[..]);
    keystream_data.extend_from_slice(&nonce);
    let keystream = hmac(&key, &keystream_data);

    let cipher: Vec<u8> = session
        .iter()
        .zip(keystream.iter())
        .map(|(a, b)| a ^ b)
        .collect();

    let mut tag_data = Vec::from(&b"tag"[..]);
    tag_data.extend_from_slice(&nonce);
    tag_data.extend_from_slice(&cipher);
    let tag = hmac(&key, &tag_data);

    (
        ClaimPayload {
            nonce: hex::encode(nonce),
            cipher: hex::encode(cipher),
            tag: hex::encode(tag),
        },
        session,
    )
}

pub fn sign_request(
    key: &[u8; 32],
    method: &str,
    path: &str,
    counter: u64,
    canonical_body: &str,
) -> String {
    let data = format!("{method}\n{path}\n{counter}\n{canonical_body}");
    let tag = hmac(key, data.as_bytes());
    format!("{counter}:{}", hex::encode(tag))
}

pub fn canonical_form_body(content_type: Option<&str>, body: &[u8]) -> String {
    let content_type = content_type.unwrap_or("").to_ascii_lowercase();
    if !content_type.starts_with("application/x-www-form-urlencoded") {
        return String::new();
    }

    let body = String::from_utf8_lossy(body);
    let mut pairs: Vec<(String, String)> = body
        .split('&')
        .filter(|part| !part.is_empty())
        .map(|part| {
            let (key, value) = part.split_once('=').unwrap_or((part, ""));
            (decode_component(key), decode_component(value))
        })
        .collect();
    pairs.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)));

    pairs
        .into_iter()
        .map(|(key, value)| format!("{key}={value}"))
        .collect::<Vec<_>>()
        .join("&")
}

fn decode_component(value: &str) -> String {
    percent_decode_str(&value.replace('+', " "))
        .decode_utf8_lossy()
        .into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn claim_round_trip() {
        let code = "ABCDEFGHIJKLMNOP";
        let (payload, session) = make_claim(code);
        let key = claim_key(code);
        let nonce = hex::decode(&payload.nonce).unwrap();
        let cipher = hex::decode(&payload.cipher).unwrap();
        let tag = hex::decode(&payload.tag).unwrap();

        let mut kd = Vec::from(&b"keystream"[..]);
        kd.extend_from_slice(&nonce);
        let stream = hmac(&key, &kd);
        let recovered: Vec<u8> = cipher
            .iter()
            .zip(stream.iter())
            .map(|(a, b)| a ^ b)
            .collect();
        assert_eq!(recovered, session);

        let mut td = Vec::from(&b"tag"[..]);
        td.extend_from_slice(&nonce);
        td.extend_from_slice(&cipher);
        assert_eq!(hmac(&key, &td).to_vec(), tag);
    }

    #[test]
    fn canonical_form_is_sorted_and_decoded() {
        let body = b"univ=1&dhcp=on&name=a+b";
        assert_eq!(
            canonical_form_body(Some("application/x-www-form-urlencoded"), body),
            "dhcp=on&name=a b&univ=1"
        );
    }
}
