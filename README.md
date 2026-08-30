# easy-artnet-interface
A open-source project aiming to give access to all people a low budget and easy-to-use artnet-dmx interface node

# 🎭 Professional WT32-ETH01 Art-Net to DMX Node
![ESP32 Core](https://img.shields.io/badge/ESP32_Core-v2.0.17-red?style=for-the-badge&logo=espressif)
![License](https://img.shields.io/badge/License-CC_BY--NC--SA_4.0-blue?style=for-the-badge)

Um Node Art-Net para DMX512 de alta performance e nível industrial, desenvolvido para a placa **WT32-ETH01 (ESP32 Ethernet)**. 

Este projeto resolve os problemas clássicos de rede em eventos ao vivo: não perde a ligação à internet no seu computador (Mac/Windows), possui **DHCP Inteligente com Auto-Router**, suporte **mDNS**, **proteção contra apagão DMX (40Hz constante)** e **Reset de Fábrica sem botões**.

---

## 🚀 Principais Características

* **Ethernet Fiável:** Baseado no chip LAN8720 (ligação por cabo super estável via UDP).
* **DHCP Inteligente com Auto-Router:**
  * Se ligado a um Router/Switch: obtém IP automaticamente via DHCP.
  * Se ligado direto ao PC/Mac (sem router): **a placa transforma-se num Servidor DHCP automático** (Gama `10.0.0.x`), permitindo ligação imediata *Plug & Play*.
* **Sem Conflito de Internet:** O servidor DHCP interno da placa desativa o envio de Gateway/DNS, permitindo que o Mac/PC continue a usar o Wi-Fi para aceder à internet (WhatsApp, navegação, etc.) enquanto controla a iluminação por cabo.
* **Acesso por Nome (mDNS):** Aceda ao painel via `http://dmx.local` no navegador sem precisar de saber o IP.
* **Painel Web de Configuração:** Interface responsiva para alterar Universo Art-Net, alternar entre DHCP e IP Fixo.
* **Proteção DMX Contínua (40 FPS / 25ms):** Se a rede falhar ou o software de luz fechar, o ESP32 continua a enviar a última cena DMX aos aparelhos, evitando apagões ou piscadelas no palco.
* **Smart Factory Reset:** Reset de fábrica por ciclo de energia (ligar/desligar a placa 2 vezes seguidas rapidamente), eliminando a necessidade de botões físicos.
* **Logging Profissional:** Utiliza o sistema de logs nativo do ESP32 (`log_i`, `log_w`, `log_e`), eliminando o consumo de CPU em produção.

---

## 🛠️ Esquema de Hardware & Pinos

### Componentes Necessários
1. Módulo **WT32-ETH01** (ESP32 com Ethernet).
2. Transcetor RS-485: **Módulo MAX485 padrão** (usado e validado neste projeto).
3. Fêmea XLR 3 pinos ou 5 pinos.
4. Fonte de alimentação 5V DC ou conector USB de escolha.
5. (opcional) Programador esp32 cp2102

> [!WARNING]
> **SOBRE O MÓDULO RS-485**
> O esquema de ligações abaixo foi desenhado e testado especificamente para o **módulo MAX485 clássico**. Se optar por utilizar outros chips (como o SN75176) ou módulos RS-485 isolados de nível industrial, **precisará de adaptar as ligações físicas**. Alguns módulos isolados gerem o sinal de *Enable* automaticamente, o que significa que o pino `EN` (IO04) do ESP32 poderá ter de ser ignorado ou ligado de forma diferente no seu hardware.

### Conexões (WT32-ETH01 ↔ Transcetor MAX485)

| Sinal | Pino WT32-ETH01 | Pino MAX485 | Descrição |
| :--- | :--- | :--- | :--- |
| **TX** | `TXD` | DI (Data In) | Transmissão de dados DMX |
| **EN** | `IO04` | DE / RE | Enable (Habilitar Transmissão) |
| **RX** | `RXD` | RO (Receiver Out)| Reservado / Leitura |
| **VCC** | `5V` | VCC | Alimentação 5V |
| **GND** | `GND` | GND | Terra |


> **Conexão DMX (Saída XLR):** 
> * MAX485 **A** ➔ XLR Pino 3 (DMX +)  
> * MAX485 **B** ➔ XLR Pino 2 (DMX -)  
> * MAX485 **GND** ➔ XLR Pino 1 (Shield / Terra)

---

## 💻 Como Compilar e Enviar o Código

### Requisitos (Arduino IDE)

> [!IMPORTANT]
> **VERSÃO DO CORE DO ESP32 OBRIGATÓRIA**
> Este projeto manipula o núcleo de rede a baixo nível para criar o Servidor DHCP. O código foi construído e otimizado para o **ESP32 Core versão 2.x (recomenda-se a 2.0.17)**. 
> Instalar a versão 3.x mais recente no *Boards Manager* causará erros de compilação na variável `dhcps_offer_option`.

1. Instalar o suporte para **ESP32** na Arduino IDE via Boards Manager (`esp32` by Espressif Systems).
2. Selecionar a placa: **ESP32 Dev Module**.
3. Definir a partição de memória: `Huge APP` ou `Default 4MB with spiffs`.
4. **Bibliotecas Externas Necessárias:**
   * `esp_dmx` (por Mitch Weiss) - *Utilizar a versão mais recente.*
   * `ArtnetWifi` (por Hideaki Tai) - *Utilizar a versão mais recente.*

### Configuração do Debug / Logs
Em **Ferramentas (Tools) > Core Debug Level**:
* Escolha `Info` ou `Debug` durante os testes.
* Escolha `None` ao gravar a versão final de produção para desempenho máximo.

---

## 📖 Guia de Utilização

### 1. Primeiro Arranque (Plug & Play)
1. Ligue o cabo Ethernet entre a placa e o seu computador (ou Switch).
2. Abra o navegador web e aceda a: **`http://dmx.local`**
3. Configure o Universo Art-Net pretendido (Padrão: `0`).

### 2. Softwares Recomendados
O Node funciona com qualquer software compatível com o protocolo Art-Net I, II, III ou IV:
* **QLC+**
* **Resolume Arena**
* **GrandMA2 / GrandMA3 (onPC)**
* **Onyx (Obsidian)**
* **MadMapper / Chamsys / FreeStyler**

### 3. Reset de Fábrica (Factory Reset)
Se perder o acesso ao IP ou configurar uma rede incorreta:
1. Ligue a placa à energia.
2. Assim que o LED piscar, desligue da tomada.
3. Repita este processo **4 vezes consecutivas** (em menos de 4 segundos em cada tentativa).
4. Na 4ª vez, deixe a placa ligada: a memória interna será apagada e o sistema voltará aos padrões de fábrica (DHCP ativo e rede `10.0.0.1` em fallback).

---

## 📜 Licença

Este projeto está licenciado sob a licença **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)**.

* **Gratuito** para uso pessoal, educacional e por entusiastas/hobbistas.
* **Proibida a comercialização** ou venda de produtos/firmwares derivados sem autorização prévia do autor.

**Para mais informações consultar o ficheiro [LICENSE](LICENSE)**

---
*Desenvolvido com foco em alta performance e fiabilidade para eventos ao vivo.*