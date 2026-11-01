use crate::{auth, discovery, model::Device, state::AppState};

pub async fn claim_device(state: &AppState, id: &str, code: &str) -> Result<Device, String> {
    let device = state
        .get(id)
        .await
        .ok_or_else(|| "dispositivo não encontrado".to_string())?;
    let code = auth::normalize_code(code);
    if code.len() != 16 {
        return Err("claim code inválido (16 caracteres)".to_string());
    }

    let (payload, session) = auth::make_claim(&code);
    let url = format!("http://{}:{}/api/claim", device.ip, device.port);
    let response = state
        .http
        .post(url)
        .json(&payload)
        .send()
        .await
        .map_err(|error| format!("placa inacessível: {error}"))?;

    if !response.status().is_success() {
        return Err(format!("placa recusou o claim: HTTP {}", response.status()));
    }

    state.auth.save(&device.id, &code, &session);
    discovery::refresh_info(state, device.ip.clone(), device.port).await;
    state
        .get(id)
        .await
        .ok_or_else(|| "dispositivo desapareceu".to_string())
}
