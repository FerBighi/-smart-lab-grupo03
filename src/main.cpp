#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include "DHT.h"

// =========================================================================
// 1. CONFIGURAÇÕES DE REDE LOCAL (LAN) - PREENCHER NA AULA
// =========================================================================
const char* ssid = "SENAI-DEV2";
const char* password = "desenvolvimento";

// Se enviar para o PC do seu grupo (ponte para G7), use o IP do seu PC:
// Se enviar direto para o Grupo 7, coloque o IPv4 do Grupo 7 do quadro:
const char* serverUrl = "http://0.xxx";

// =========================================================================
// 2. MAPEAMENTO DE PINOS (GRUPO 03 + TESTE 3 SPRINT 2)
// =========================================================================
#define PIN_DHT 4
#define PIN_BOTAO_SAIDA 14     // Botoeira externa na protoboard
#define PIN_BOTAO_BOOT 0       // Botão físico BOOT na placa ESP32 (Teste 3)
#define PIN_RELE_FECHADURA 26  // Módulo Relé Local (Obrigatório)
#define PIN_LED_ALARME 25      // LED Vermelho de Alarme
#define PIN_LED_ONBOARD 2      // LED Azul interno do ESP32 (Obrigatório)

#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// Variáveis de Controle de Acesso e Segurança
bool modoArmado = false;          // false = Aula Ativa | true = Lab Encerrado (Armado)
bool portaDestravada = false;     // Estado atual do relé da fechadura
bool alarmeIntrusao = false;      // Estado de alerta por tentativa de abertura bloqueada

unsigned long tempoDestravamento = 0;
const unsigned long DURACAO_ABERTURA_MS = 5000; // 5 segundos temporizados
unsigned long ultimoEnvio = 0;
unsigned long ultimoDebounce = 0;
unsigned long ultimaTentativaWifi = 0;
bool estadoBotaoAnterior = HIGH;

// Protótipos das funções
void liberarFechadura(String origem);
void trancarEArmar(String origem);
void enviarTelemetria(String eventoLog);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELE_FECHADURA, OUTPUT);
  pinMode(PIN_LED_ALARME, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);
  pinMode(PIN_BOTAO_SAIDA, INPUT_PULLUP);
  pinMode(PIN_BOTAO_BOOT, INPUT_PULLUP);

  dht.begin();

  // Inicia com a porta trancada e sem alarmes
  digitalWrite(PIN_RELE_FECHADURA, LOW);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  digitalWrite(PIN_LED_ALARME, LOW);

  // Conexão Wi-Fi Local com timeout (não trava a borda se o roteador demorar)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi Local");
  unsigned long inicioConexao = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicioConexao < 10000)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado! IP ESP32: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[AVISO BORDA] Wi-Fi indisponivel no boot. Operando em modo autonomo!");
  }

  // Configuração OTA Local
  ArduinoOTA.setHostname("ESP32-LAB-GRUPO-03");
  ArduinoOTA.setPassword("senai123");
  ArduinoOTA.begin();

  Serial.println("=== GRUPO 3: CONTROLE DE ACESSO E SEGURANCA INICIADO ===");
}

void loop() {
  ArduinoOTA.handle(); // Mantém atualização sem fio ativa sem bloquear
  unsigned long agora = millis();

  // 0. Reconexão Automática do Wi-Fi sem travar a borda (Teste 1 - Bug Hunt)
  if (WiFi.status() != WL_CONNECTED && (agora - ultimaTentativaWifi >= 5000)) {
    ultimaTentativaWifi = agora;
    Serial.println("[REDE] Wi-Fi desconectado. Tentando reconectar automaticamente...");
    WiFi.disconnect();
    WiFi.reconnect();
  }

  // 1. Leitura dos Botões Físicos (Botoeira GPIO 14 OU Botão BOOT GPIO 0)
  bool leituraBotao = (digitalRead(PIN_BOTAO_SAIDA) == LOW || digitalRead(PIN_BOTAO_BOOT) == LOW) ? LOW : HIGH;

  if (leituraBotao == LOW && estadoBotaoAnterior == HIGH && (agora - ultimoDebounce > 300)) {
    ultimoDebounce = agora;

    if (!modoArmado) {
      liberarFechadura("Botao Fisico Local");
    } else {
      alarmeIntrusao = true;
      digitalWrite(PIN_LED_ALARME, HIGH);
      Serial.println("\n[ALERTA CRITICO] Tentativa de abertura com laboratorio ARMADO!");
      enviarTelemetria("TENTATIVA_INTRUSAO");
    }
  }
  estadoBotaoAnterior = leituraBotao;

  // 2. Temporizador Automático da Fechadura (Tranca após 5 segundos)
  if (portaDestravada && (agora - tempoDestravamento >= DURACAO_ABERTURA_MS)) {
    portaDestravada = false;
    digitalWrite(PIN_RELE_FECHADURA, LOW);
    digitalWrite(PIN_LED_ONBOARD, LOW);
    Serial.println("[AUTO-LOCK] Tempo de 5s expirado. Fechadura TRANCADA.");
    enviarTelemetria("PORTA_TRANCADA_AUTO");
  }

  // 3. Pisca o LED Vermelho caso o alarme de intrusão esteja ativo
  if (alarmeIntrusao) {
    digitalWrite(PIN_LED_ALARME, (agora / 250) % 2);
  }

  // 4. Ciclo de Telemetria obrigatório a cada 2 segundos (2000ms)
  if (agora - ultimoEnvio >= 2000) {
    ultimoEnvio = agora;
    enviarTelemetria("MONITORAMENTO_ROTINA");
  }
}

void liberarFechadura(String origem) {
  portaDestravada = true;
  alarmeIntrusao = false;
  tempoDestravamento = millis();
  digitalWrite(PIN_RELE_FECHADURA, HIGH);
  digitalWrite(PIN_LED_ONBOARD, HIGH);
  digitalWrite(PIN_LED_ALARME, LOW);
  Serial.println("\n[ACESSO LIBERADO] Origem: " + origem + " | Destravada por 5s.");
  enviarTelemetria("ACESSO_LIBERADO");
}

void trancarEArmar(String origem) {
  portaDestravada = false;
  alarmeIntrusao = false;
  modoArmado = true;
  digitalWrite(PIN_RELE_FECHADURA, LOW);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  digitalWrite(PIN_LED_ALARME, LOW);
  Serial.println("\n[SISTEMA ENCERRADO/ARMADO] Origem: " + origem);
}

void enviarTelemetria(String eventoLog) {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  // Filtro NaN obrigatório (Teste 4 - Bug Hunt)
  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("[AVISO SENSOR] Falha na leitura do DHT22 (GPIO 4) - Aplicando filtro NaN!");
    temperatura = 24.5;
    umidade = 55.0;
  }

  // Definição do Semáforo e Código Numérico Padrão
  String semaforo = "NORMAL";
  int codigoEstado = 0; // 0 = Trancada | 1 = Aberta Autorizada | 2 = Violação/Alarme

  if (alarmeIntrusao) {
    semaforo = "CRITICO";
    codigoEstado = 2;
  } else if (portaDestravada) {
    semaforo = "ATENCAO";
    codigoEstado = 1;
  } else if (temperatura > 28.0) {
    semaforo = "ATENCAO";
  }

  // Montagem do Pacote JSON Padronizado da Turma
  String json = "{";
  json += "\"grupo_id\":\"grupo_03\",";
  json += "\"nome_subsistema\":\"Controle_Acesso_Porta\",";
  json += "\"ip_local\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"mac_address\":\"" + WiFi.macAddress() + "\",";
  json += "\"firmware_versao\":\"v1.0-LAN\",";
  json += "\"temperatura\":" + String(temperatura, 1) + ",";
  json += "\"umidade\":" + String(umidade, 1) + ",";
  json += "\"variavel_principal_nome\":\"Estado_Porta_Codigo\",";
  json += "\"variavel_principal_valor\":" + String(codigoEstado) + ",";
  json += "\"rele_ativo\":" + String(portaDestravada ? "true" : "false") + ",";
  json += "\"status_alerta\":\"" + semaforo + "\",";
  json += "\"evento_auditoria\":\"" + eventoLog + "\"";
  json += "}";

  Serial.println("POST /api/telemetria -> " + json);

  // Envio HTTP POST real com timeout curto (1500ms) para não travar a borda
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(json);
    if (httpCode > 0) {
      String resposta = http.getString();

      if (resposta.indexOf("\"comando\":\"LIGAR\"") >= 0) {
        liberarFechadura("Dashboard Web");
      } else if (resposta.indexOf("\"comando\":\"PARAR\"") >= 0) {
        // Resposta ao comando individual PARAR ou Macro Geral do Grupo 7 (Teste 5)
        trancarEArmar("Comando Remoto / G7");
      } else if (resposta.indexOf("\"comando\":\"ARMAR\"") >= 0) {
        modoArmado = true;
        Serial.println("[MODO] Laboratorio ARMADO via Dashboard.");
      } else if (resposta.indexOf("\"comando\":\"DESARMAR\"") >= 0) {
        modoArmado = false;
        alarmeIntrusao = false;
        digitalWrite(PIN_LED_ALARME, LOW);
        Serial.println("[MODO] Laboratorio DESARMADO (Aula Ativa).");
      }
    } else {
      Serial.println("[HTTP] Servidor offline. Mantendo operacao autonoma na borda.");
    }
    http.end();
  }
}
