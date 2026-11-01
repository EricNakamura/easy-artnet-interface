mod auth;
mod claim;
mod dashboard;
mod discovery;
mod model;
mod proxy;
mod state;
mod vault;

use std::time::Duration;

use axum::{
    extract::State,
    http::StatusCode,
    response::Json,
    routing::{any, get, post},
    Router,
};
use clap::Parser;

use model::{AddDevice, ClaimRequest, Device};
use state::AppState;

#[derive(Debug, Parser)]
#[command(about = "Hub local Easy ArtNet")]
struct Args {
    /// Porta do dashboard/proxy em 127.0.0.1.
    #[arg(long, default_value_t = 8080)]
    port: u16,

    /// Não abrir o browser automaticamente.
    #[arg(long)]
    no_open: bool,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    let state = AppState::new();

    discovery::start_mdns(state.clone());
    discovery::start_beacon(state.clone());

    let scan_state = state.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_secs(3)).await;
        if scan_state.list().await.is_empty() {
            discovery::spawn_scan(scan_state);
        }
    });

    let app = Router::new()
        .route("/", get(dashboard::index))
        .route("/api/devices", get(list_devices))
        .route("/api/devices/add", post(add_device))
        .route("/api/devices/:id/claim", post(claim_device))
        .route("/api/devices/:id/diagnostics", post(enable_diagnostics))
        .route("/api/scan", post(scan))
        .route("/d/:id", get(proxy::redirect_device))
        .route("/d/:id/", any(proxy::proxy_root))
        .route("/d/:id/*path", any(proxy::proxy_wildcard))
        .with_state(state);

    let listener = tokio::net::TcpListener::bind(("127.0.0.1", args.port)).await?;
    let url = format!("http://localhost:{}/", args.port);
    println!("Easy ArtNet Hub: {url}");
    if !args.no_open {
        let _ = open::that(&url);
    }

    axum::serve(listener, app).await?;
    Ok(())
}

async fn list_devices(State(state): State<AppState>) -> Json<Vec<Device>> {
    Json(state.list().await)
}

async fn scan(State(state): State<AppState>) -> StatusCode {
    discovery::spawn_scan(state);
    StatusCode::ACCEPTED
}

async fn add_device(
    State(state): State<AppState>,
    Json(request): Json<AddDevice>,
) -> Result<Json<Device>, (StatusCode, String)> {
    let raw = request.address.trim();
    if raw.is_empty() {
        return Err((StatusCode::BAD_REQUEST, "Endereço vazio".to_string()));
    }

    let (ip, port) = match raw.rsplit_once(':') {
        Some((ip, port)) if port.parse::<u16>().is_ok() && !ip.contains(':') => {
            (ip.to_string(), port.parse::<u16>().unwrap())
        }
        _ => (raw.to_string(), 80),
    };

    let info = discovery::fetch_info(&state.http, &ip, port, Duration::from_secs(3))
        .await
        .ok_or_else(|| {
            (
                StatusCode::BAD_REQUEST,
                format!("Sem /api/info em http://{ip}:{port}/api/info"),
            )
        })?;

    let device = discovery::device_from_info(info, &ip, port, "manual");
    state.upsert(device.clone()).await;
    Ok(Json(device))
}

async fn claim_device(
    State(state): State<AppState>,
    axum::extract::Path(id): axum::extract::Path<String>,
    Json(request): Json<ClaimRequest>,
) -> Result<Json<Device>, (StatusCode, String)> {
    claim::claim_device(&state, &id, &request.code)
        .await
        .map(Json)
        .map_err(|error| (StatusCode::BAD_REQUEST, error))
}

async fn enable_diagnostics(
    State(state): State<AppState>,
    axum::extract::Path(id): axum::extract::Path<String>,
) -> Result<Json<serde_json::Value>, (StatusCode, String)> {
    let url = proxy::enable_diagnostics(&state, &id).await?;
    Ok(Json(serde_json::json!({ "url": url })))
}
