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
- Proxy `http://localhost:8080/d/<id>/` para o painel; o formulário do painel usa `action="salvar"` relativo.
- Bind em `127.0.0.1`; sem exposição na LAN.
- Sem cloud, sem Wi-Fi, sem botões e sem display.

## Limitações deste MVP

- **Claim code/auth ainda não implementados** (issues #2, #3, #6). O `/api/info` mostra `claimed:false` e o painel direto ainda está aberto na LAN. Não usar em produção até #6.
- O scan cobre no máximo /24 por interface; redes /16 só a /24 que contém o IP.
- Guest Wi-Fi/client isolation/VLAN sem routing não são suportados; o hub mostra o que encontrou e o IP manual é o último recurso.
- mDNS depende do serviço anunciado pelo firmware; se o router bloquear multicast, o beacon/scan cobrem.
- Testado com um dispositivo HTTP simulado; falta validação na placa real.

## Firmware esperado

O firmware no env `w5500_sem_int` expõe:

- `GET /api/info` → `{id, mac, ip, port, fw, claimed, name}`;
- beacon UDP 6456 a cada 2 s;
- serviço mDNS `_easyartnet._tcp` com TXT `id`/`fw`;
- painel com `action="salvar"`.

Ver issue #5.

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
`127.0.0.1:18080`, adiciona o dispositivo, abre o painel e testa o POST
`/salvar`.

## Mapa / issues

- Mapa: #1
- Firmware identidade/descoberta: #5
- Hub MVP: #7
- Claim/auth: #2, #3, #6
- Lab: #8, #9
- Ecossistema: #10
