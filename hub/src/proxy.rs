use axum::{
    body::Body,
    extract::{OriginalUri, Path, State},
    http::{header, HeaderMap, Method, StatusCode},
    response::{Redirect, Response},
};
use bytes::Bytes;

use crate::state::AppState;

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

    let method = reqwest::Method::from_bytes(method.as_str().as_bytes())
        .map_err(|_| (StatusCode::BAD_REQUEST, "Método inválido".to_string()))?;
    let mut request = state.http.request(method, url);

    if let Some(content_type) = headers.get(header::CONTENT_TYPE) {
        request = request.header(header::CONTENT_TYPE, content_type);
    }
    if let Some(accept) = headers.get(header::ACCEPT) {
        request = request.header(header::ACCEPT, accept);
    }
    if !body.is_empty() {
        request = request.body(body);
    }

    let response = request.send().await.map_err(|error| {
        (
            StatusCode::BAD_GATEWAY,
            format!("Placa inacessível: {error}"),
        )
    })?;
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
