# Smart Lab SENAI — Subsistema 03: Controle de Acesso e Segurança

## 1. Contextualização e Problema Resolvido
O Subsistema 03 automatiza o **controle de acesso e monitoramento de segurança da porta** do Laboratório de Informática. O objetivo é impedir entradas não autorizadas, alertar imediatamente caso a porta seja aberta fora do horário de aula (modo armado) ou esquecida aberta, e permitir o destravamento/travamento remoto tanto pelo dashboard individual quanto pelo comando mestre do professor (Grupo 7).

## 2. Arquitetura IoT Local (On-Premises)
O sistema opera **100% em rede local (LAN)** sem dependência de nuvem externa, garantindo baixa latência (< 2 segundos), privacidade de dados e autonomia operacional na borda:
- **Camada de Borda (ESP32):** Realiza a leitura contínua dos sensores e aciona o Módulo Relé local de forma autônoma mesmo em caso de queda de Wi-Fi ou servidor.
- **Camada de Agregação Local (`server.js`):** Servidor Node.js/Express que recebe telemetria via HTTP POST a cada 2 segundos, filtra leituras inválidas (`NaN`), armazena histórico (50 leituras) e log de auditoria, e encaminha dados ao Supervisório Geral (Grupo 7).
- **Camada de Visualização (`public/dashboard.html`):** Interface responsiva com indicador `ONLINE/OFFLINE`, faixa de diagnóstico dinâmico, botões de acionamento remoto e gráfico histórico Chart.js (`height: 220px`).

## 3. Hardware e Pinagem (ESP32)

| Componente                   | Pino ESP32      | Função no Circuito                                             |
| :--------------------------- | :-------------- | :------------------------------------------------------------- |
| **Módulo Relé Local**        | `GPIO 26`       | Acionamento da trava elétrica / simulação de abertura da porta |
| **Sensor DHT11/22**          | `GPIO 4`        | Monitoramento de temperatura e umidade junto à porta/rack      |
| **Botão Físico Local**       | `GPIO 0 (BOOT)` | Acionamento manual local sincronizado com o dashboard          |
| **LED Indicador Interno**    | `GPIO 2`        | Sinalização visual do estado do relé na placa                  |
| **Sensor de Porta / Acesso** | `GPIO 14 / 27`  | Detecção de abertura autorizada ou intrusão (modo armado)      |

## 4. Protocolos e Segurança
- **HTTP REST (JSON):** Endpoints `POST /api/telemetria`, `GET /api/telemetria` e `POST /api/comando` com identificação `"grupo_id": "grupo_03"`.
- **Atualização Remota (ArduinoOTA):** Configurada com hostname e senha para atualização de firmware via rede Wi-Fi local.
- **Resiliência (Bug Hunt Validado):**
  - Reconexão automática de Wi-Fi sem travar o `loop()`.
  - Tratamento de `isnan()` no firmware e no back-end para proteger os gráficos.
  - Desarme imediato do relé ao receber `"comando": "PARAR"` do painel mestre (Grupo 7).
