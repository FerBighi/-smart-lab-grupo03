#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <DHT.h>

// =========================================================================
// 1. CONFIGURAÇÕES DE REDE LOCAL (LAN) - AJUSTE ANTES DE ENVIAR PARA O ESP32
// =========================================================================
const char* ssid = "NOME_DO_WIFI_DO_LAB";
const char* password = "SENHA_DO_WIFI_DO_LAB";
const char* serverUrl = "http://192.168.0"; // IP do PC com server.js

// =========================================================================
// 2. MAPEAMENTO DE PINOS (GRUPO 03 + LED INTERNO OBRIGATÓRIO NA SPRINT 1)
// =========================================================================
#define PIN_DHT 4
#define PIN_BOTAO_SAIDA 14
#define PIN_RELE_FECHADURA 26
#define PIN_LED_ALARME 25
#define PIN_LED_ONBOARD 2

#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// Variáveis de Controle de Acesso e Segurança
bool modoArmado = false;          // false = Aula Ativa | true = Lab Encerrado (Bloqueado)
bool portaDestravada = false;     // Estado atual do relé da fechadura
bool alarmeIntrusao = false;      // Estado de alerta por tentativa de abertura bloqueada

unsigned long tempoDestravamento = 0;
const unsigned long DURACAO_ABERTURA_MS = 5000; // 5 segundos temporizados
unsigned long ultimoEnvio = 0;
unsigned long ultimoDebounce = 0;
bool estadoBotaoAnterior = HIGH;

// Protótipos das funções (Obrigatório para compilar no PlatformIO)
void liberarFechadura(String origem);
void enviarTelemetria(String eventoLog);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELE_FECHADURA, OUTPUT);
  pinMode(PIN_LED_ALARME, OUTPUT);
  pinMode(PIN_LED_ONBOARD, OUTPUT);
  pinMode(PIN_BOTAO_SAIDA, INPUT_PULLUP);

  dht.begin();

  // Inicia com a porta trancada e sem alarmes
  digitalWrite(PIN_RELE_FECHADURA, LOW);
  digitalWrite(PIN_LED_ONBOARD, LOW);
  digitalWrite(PIN_LED_ALARME, LOW);

  // Conexão Wi-Fi Local (Etapa 2.1)
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi Local");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP ESP32: " + WiFi.localIP().toString());

  // Configuração OTA Local (Etapa 2.4)
  ArduinoOTA.setHostname("ESP32-LAB-GRUPO-03");
  ArduinoOTA.setPassword("senai123");
  ArduinoOTA.begin();

  Serial.println("=== GRUPO 3: CONTROLE DE ACESSO E SEGURANCA INICIADO ===");
}

void loop() {
  ArduinoOTA.handle(); // Mantém atualização sem fio ativa sem bloquear
  unsigned long agora = millis();

  // 1. Leitura da Botoeira Interna de Saída (GPIO 14)
  bool leituraBotao = digitalRead(PIN_BOTAO_SAIDA);
  if (leituraBotao == LOW && estadoBotaoAnterior == HIGH && (agora - ultimoDebounce > 300)) {
    ultimoDebounce = agora;

    if (!modoArmado) {
      liberarFechadura("Botoeira Interna");
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

void enviarTelemetria(String eventoLog) {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  if (isnan(temperatura) || isnan(umidade)) {
    temperatura = 24.5; // Valor padrão caso o sensor falhe na bancada
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

  // Montagem do Pacote JSON Padronizado da Turma (Etapa 2.1)
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

  // Envio HTTP POST real para o Servidor Node.js
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(json);
    if (httpCode > 0) {
      String resposta = http.getString();
      // Executa comandos vindos do Dashboard Web (Etapa 2.1)
      if (resposta.indexOf("\"comando\":\"LIGAR\"") >= 0) {
        liberarFechadura("Dashboard Web");
      } else if (resposta.indexOf("\"comando\":\"PARAR\"") >= 0) {
        portaDestravada = false;
        alarmeIntrusao = false;
        digitalWrite(PIN_RELE_FECHADURA, LOW);
        digitalWrite(PIN_LED_ONBOARD, LOW);
        digitalWrite(PIN_LED_ALARME, LOW);
      } else if (resposta.indexOf("\"comando\":\"ARMAR\"") >= 0) {
        modoArmado = true;
      } else if (resposta.indexOf("\"comando\":\"DESARMAR\"") >= 0) {
        modoArmado = false;
        alarmeIntrusao = false;
        digitalWrite(PIN_LED_ALARME, LOW);
      }
    }
    http.end();
  }
}
