use axum::{
    body::Body,
    extract::{OriginalUri, Path, State},
    http::{header, HeaderMap, Method, StatusCode},
    response::{Redirect, Response},
};
use bytes::Bytes;

use crate::{auth, claim, model::Device, state::AppState};

pub async fn redirect_device(Path(id): Path<String>) -> Redirect {
    Redirect::temporary(&format!("/d/{id}/"))
}

pub async fn proxy_root(
    State(state): State<AppState>,
    Path(id): Path<String>,
    method: Method,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    body: Bytes,
) -> Result<Response, (StatusCode, String)> {
    proxy(state, id, String::new(), method, uri, headers, body).await
}

pub async fn proxy_wildcard(
    State(state): State<AppState>,
    Path((id, path)): Path<(String, String)>,
    method: Method,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    body: Bytes,
) -> Result<Response, (StatusCode, String)> {
    proxy(state, id, path, method, uri, headers, body).await
}

async fn proxy(
    state: AppState,
    id: String,
    path: String,
    method: Method,
    uri: axum::http::Uri,
    headers: HeaderMap,
    body: Bytes,
) -> Result<Response, (StatusCode, String)> {
    let device = state.get(&id).await.ok_or((
        StatusCode::NOT_FOUND,
        "Dispositivo não encontrado".to_string(),
    ))?;

    let credentials = state.auth.load(&device.id).ok_or((
        StatusCode::UNAUTHORIZED,
        "Dispositivo não reivindicado; introduz o claim code no hub.".to_string(),
    ))?;

    let mut url = format!(
        "http://{}:{}/{}",
        device.ip,
        device.port,
        path.trim_start_matches('/')
    );
    if let Some(query) = uri.query() {
        url.push('?');
        url.push_str(query);
    }

    let device_path = format!("/{}", path.trim_start_matches('/'));
    let canonical = auth::canonical_form_body(
        headers
            .get(header::CONTENT_TYPE)
            .and_then(|value| value.to_str().ok()),
        &body,
    );

    let response = send_signed(
        &state,
        &device,
        &method,
        &device_path,
        &url,
        &headers,
        &body,
        &canonical,
    )
    .await?;

    let response = if response.status() == StatusCode::UNAUTHORIZED {
        let code = credentials.code.clone();
        claim::claim_device(&state, &device.id, &code)
            .await
            .map_err(|error| {
                (
                    StatusCode::UNAUTHORIZED,
                    format!("Sessão inválida e re-claim falhou: {error}"),
                )
            })?;
        send_signed(
            &state,
            &device,
            &method,
            &device_path,
            &url,
            &headers,
            &body,
            &canonical,
        )
        .await?
    } else {
        response
    };

    to_response(response).await
}

#[allow(clippy::too_many_arguments)]
async fn send_signed(
    state: &AppState,
    device: &Device,
    method: &Method,
    path: &str,
    url: &str,
    headers: &HeaderMap,
    body: &Bytes,
    canonical: &str,
) -> Result<reqwest::Response, (StatusCode, String)> {
    let credentials = state.auth.load(&device.id).ok_or((
        StatusCode::UNAUTHORIZED,
        "Dispositivo não reivindicado.".to_string(),
    ))?;
    let counter = state.auth.next_counter(&device.id).ok_or((
        StatusCode::UNAUTHORIZED,
        "Sessão não encontrada.".to_string(),
    ))?;
    let auth_header =
        auth::sign_request(&credentials.key, method.as_str(), path, counter, canonical);

    let reqwest_method = reqwest::Method::from_bytes(method.as_str().as_bytes())
        .map_err(|_| (StatusCode::BAD_REQUEST, "Método inválido".to_string()))?;
    let mut request = state
        .http
        .request(reqwest_method, url)
        .header("X-EasyArtNet-Auth", auth_header);

    if let Some(content_type) = headers.get(header::CONTENT_TYPE) {
        request = request.header(header::CONTENT_TYPE, content_type);
    }
    if let Some(accept) = headers.get(header::ACCEPT) {
        request = request.header(header::ACCEPT, accept);
    }
    if !body.is_empty() {
        request = request.body(body.clone());
    }

    request.send().await.map_err(|error| {
        (
            StatusCode::BAD_GATEWAY,
            format!("Placa inacessível: {error}"),
        )
    })
}

pub async fn enable_diagnostics(
    state: &AppState,
    id: &str,
) -> Result<String, (StatusCode, String)> {
    let device = state.get(id).await.ok_or((
        StatusCode::NOT_FOUND,
        "Dispositivo não encontrado".to_string(),
    ))?;
    let credentials = state.auth.load(&device.id).ok_or((
        StatusCode::UNAUTHORIZED,
        "Dispositivo não reivindicado.".to_string(),
    ))?;

    let url = format!(
        "http://{}:{}/api/diagnostics/enable",
        device.ip, device.port
    );
    let method = Method::POST;
    let path = "/api/diagnostics/enable";
    let headers = HeaderMap::new();
    let body = Bytes::new();
    let canonical = String::new();

    let response = send_signed(
        state, &device, &method, path, &url, &headers, &body, &canonical,
    )
    .await?;

    let response = if response.status() == StatusCode::UNAUTHORIZED {
        claim::claim_device(state, &device.id, &credentials.code)
            .await
            .map_err(|error| {
                (
                    StatusCode::UNAUTHORIZED,
                    format!("Re-claim falhou: {error}"),
                )
            })?;
        send_signed(
            state, &device, &method, path, &url, &headers, &body, &canonical,
        )
        .await?
    } else {
        response
    };

    if !response.status().is_success() {
        return Err((
            StatusCode::BAD_GATEWAY,
            format!("Placa recusou diagnóstico: HTTP {}", response.status()),
        ));
    }

    Ok(format!("http://{}:{}/diagnostico", device.ip, device.port))
}

async fn to_response(response: reqwest::Response) -> Result<Response, (StatusCode, String)> {
    let status =
        StatusCode::from_u16(response.status().as_u16()).unwrap_or(StatusCode::BAD_GATEWAY);
    let mut builder = Response::builder().status(status);

    for name in [
        header::CONTENT_TYPE,
        header::CACHE_CONTROL,
        header::LOCATION,
        header::SET_COOKIE,
    ] {
        if let Some(value) = response.headers().get(&name) {
            builder = builder.header(&name, value);
        }
    }

    let body = response.bytes().await.map_err(|error| {
        (
            StatusCode::BAD_GATEWAY,
            format!("Resposta inválida da placa: {error}"),
        )
    })?;

    builder.body(Body::from(body)).map_err(|_| {
        (
            StatusCode::INTERNAL_SERVER_ERROR,
            "Resposta inválida".to_string(),
        )
    })
}
