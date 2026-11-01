# Easy ArtNet Hub (MVP local)

Hub local em Rust para descobrir e abrir o painel das interfaces Easy ArtNet
sem depender de `.local`, DNS do router ou cloud.

O browser fala com `http://localhost:8080`; o hub fala mDNS/beacon/scan e faz
proxy do painel da placa em `http://localhost:8080/d/<id>/`.

## Correr

```bash
cd hub
cargo run --release
```

Abre `http://localhost:8080/`. Para não abrir o browser:

```bash
cargo run --release -- --no-open
```

## O que o MVP faz

- Descoberta por:
  - serviço mDNS `_easyartnet._tcp.local` (o hub fala mDNS direto, não usa o resolvedor do OS);
  - beacon UDP 6456 enviado pela placa;
  - scan da sub-rede (`/api/info` em cada IPv4, até /24 por interface);
  - IP manual no dashboard.
- Dashboard em `http://localhost:8080/` com dispositivos, IP, firmware, claim e fontes.
- Claim code + autenticação:
  - claim one-time com chave de sessão cifrada pelo claim code;
  - pedidos do proxy assinados com HMAC-SHA256 + contador;
  - painel direto devolve 401; o proxy do hub autentica;
  - “Reconectar” permite repetir o claim com o código guardado;
  - “Diagnóstico” abre 10 min de login direto por claim code na placa.
- Cofre: keyring do sistema por omissão; memória com `EASYARTNET_MEMORY_VAULT=1` (testes/headless).
- Proxy `http://localhost:8080/d/<id>/` para o painel; o formulário do painel usa `action="salvar"` relativo.
- Bind em `127.0.0.1`; sem exposição na LAN.
- Sem cloud, sem Wi-Fi, sem botões e sem display.

## Limitações deste MVP

- **Claim code de produção ainda não tem etiqueta/QR nem eFuse/partição read-only**: nesta fase o código é gerado em NVS `factory`, sobrevive ao reset de config e aparece no serial (`Claim code:`). Para produto, provisionar/etiquetar e proteger (issue #2/#6).
- O cofre ainda não tem **export encriptado/backup**; só keyring (ou memória). Perder o código e a etiqueta numa placa escondida continua irrecuperável.
- O scan cobre no máximo /24 por interface; redes /16 só a /24 que contém o IP.
- Guest Wi-Fi/client isolation/VLAN sem routing não são suportados; o hub mostra o que encontrou e o IP manual é o último recurso.
- mDNS depende do serviço anunciado pelo firmware; se o router bloquear multicast, o beacon/scan cobrem.
- Testado com um dispositivo HTTP simulado (claim + HMAC); falta validação na placa real e no lab.

## Firmware esperado

O firmware no env `w5500_sem_int` expõe:

- `GET /api/info` → `{id, mac, ip, port, fw, claimed, name}`;
- beacon UDP 6456 a cada 2 s;
- serviço mDNS `_easyartnet._tcp` com TXT `id`/`fw`;
- `POST /api/claim` (claim code + chave de sessão cifrada);
- HMAC-SHA256 no painel (`/` e `/salvar`);
- `/diagnostico` (login por claim code durante a janela);
- painel com `action="salvar"`.

O claim code é impresso no serial no arranque (`Claim code:`). Usa-o no
dashboard para reivindicar a placa.

Ver issues #5/#6.

## Testes

```bash
cd hub
cargo fmt --check
cargo test
cargo build --release
```

Teste manual do proxy com um dispositivo simulado:

```bash
./scripts/smoke.sh
```

O script sobe um dispositivo HTTP simulado em `127.0.0.1:18081`, o hub em
`127.0.0.1:18080`, adiciona o dispositivo, faz o claim (código
`TESTCLAIMCODE123`), abre o painel autenticado e testa o POST `/salvar`.

## Mapa / issues

- Mapa: #1
- Firmware identidade/descoberta: #5
- Hub MVP: #7
- Claim/auth: #2, #3, #6
- Lab: #8, #9
- Ecossistema: #10
