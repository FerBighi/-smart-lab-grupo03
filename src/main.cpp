#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// --- CONFIGURAÇÕES DE REDE E SERVIDOR NODE.JS ---
const char* ssid = "SENAI-DEV2";
const char* password = "desenvolvimento";
// Substitua pelo endereço ngrok ou IP da rede local onde o Node.js está rodando
const char* SERVER_URL = "http://192.168.0.103:3000/api/telemetria";

// --- DEFINIÇÃO DE PINOS ---
#define PIN_BOTOEIRA   14
#define PIN_SENSOR_PTA 27
#define PIN_DHT        15
#define PIN_RELE       26
#define PIN_LED_ALARM  25
#define PIN_BUZZER     33

#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// --- VARIÁVEIS DE CONTROLE ---
bool modoArmado = true;          // true = Trancado/Armado (Fora de aula) | false = Liberado (Em aula)
bool fechaduraDestravada = false;
unsigned long tempoDestravamento = 0;
const unsigned long DURACAO_RELE = 5000; // 5 segundos destravado

bool portaAberta = false;
bool portaAbertaAnterior = false;
unsigned long tempoPortaAbriu = 0;
const unsigned long LIMITE_PORTA_ABERTA = 15000; // 15 segundos de tolerância antes de alarme

bool alarmeAtivo = false;
String motivoAlarme = "Nenhum";

unsigned long ultimoEnvioTelemetria = 0;

// --- PROTÓTIPOS DAS FUNÇÕES ---
void acionarFechadura(String origem);
void registrarLogNodeJS(String tipo, String descricao);
void sincronizarComServidor();

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BOTOEIRA, INPUT_PULLUP);
  pinMode(PIN_SENSOR_PTA, INPUT_PULLUP); // LOW = Fechada, HIGH = Aberta
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_LED_ALARM, LOW);
  noTone(PIN_BUZZER);

  dht.begin();

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Conectado! IP: " + WiFi.localIP().toString());
}

void acionarFechadura(String origem) {
  Serial.println("[ACESSO] Fechadura destravada via: " + origem);
  fechaduraDestravada = true;
  tempoDestravamento = millis();
  digitalWrite(PIN_RELE, HIGH);

  alarmeAtivo = false;
  motivoAlarme = "Nenhum";

  registrarLogNodeJS("ABERTURA_AUTORIZADA", "Porta destravada via " + origem);
}

void loop() {
  unsigned long agora = millis();

  // 1. LEITURA DA BOTOEIRA INTERNA DE SAÍDA
  if (digitalRead(PIN_BOTOEIRA) == LOW) {
    if (!fechaduraDestravada) {
      acionarFechadura("Botoeira Interna");
    }
    delay(200);
  }

  // 2. CONTROLE DE TEMPO DO RELÉ (5 SEGUNDOS)
  if (fechaduraDestravada && (agora - tempoDestravamento >= DURACAO_RELE)) {
    fechaduraDestravada = false;
    digitalWrite(PIN_RELE, LOW);
    Serial.println("[ACESSO] Fechadura trancada novamente (5s expirados).");
  }

  // 3. LEITURA DO SENSOR DE PORTA (HIGH = Aberta, LOW = Fechada)
  portaAberta = (digitalRead(PIN_SENSOR_PTA) == HIGH);

  // Transição: Fechada -> Aberta
  if (portaAberta && !portaAbertaAnterior) {
    tempoPortaAbriu = agora;
    Serial.println("[SENSOR] Porta foi ABERTA.");

    if (modoArmado && !fechaduraDestravada) {
      alarmeAtivo = true;
      motivoAlarme = "INTRUSAO DETECTADA (Porta aberta em modo Armado!)";
      Serial.println("[ALERTA] " + motivoAlarme);
      registrarLogNodeJS("INTRUSAO", motivoAlarme);
    }
  }

  // Transição: Aberta -> Fechada
  if (!portaAberta && portaAbertaAnterior) {
    Serial.println("[SENSOR] Porta foi FECHADA.");
    if (motivoAlarme.startsWith("PORTA ESQUECIDA")) {
      alarmeAtivo = false;
      motivoAlarme = "Nenhum";
    }
  }
  portaAbertaAnterior = portaAberta;

  // Porta mantida aberta além do tempo limite (15s)
  if (portaAberta && (agora - tempoPortaAbriu >= LIMITE_PORTA_ABERTA) && !alarmeAtivo) {
    alarmeAtivo = true;
    motivoAlarme = "PORTA ESQUECIDA ABERTA (> 15s)";
    Serial.println("[ALERTA] " + motivoAlarme);
    registrarLogNodeJS("PORTA_ABERTA_TEMPO", motivoAlarme);
  }

  // 4. ATUAÇÃO DO ALARME SONORO E VISUAL
  if (alarmeAtivo) {
    bool pulso = (agora / 300) % 2 == 0;
    digitalWrite(PIN_LED_ALARM, pulso ? HIGH : LOW);
    if (pulso) {
      tone(PIN_BUZZER, 1000);
    } else {
      noTone(PIN_BUZZER);
    }
  } else {
    digitalWrite(PIN_LED_ALARM, LOW);
    noTone(PIN_BUZZER);
  }

  // 5. SINCRONIZAÇÃO COM O SERVIDOR NODE.JS A CADA 2 SEGUNDOS
  if (agora - ultimoEnvioTelemetria >= 2000) {
    ultimoEnvioTelemetria = agora;
    sincronizarComServidor();
  }
}

void registrarLogNodeJS(String tipo, String descricao) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/logs");
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["tipo"] = tipo;
  doc["descricao"] = descricao;

  String payload;
  serializeJson(doc, payload);
  http.POST(payload);
  http.end();
}

void sincronizarComServidor() {
  if (WiFi.status() != WL_CONNECTED) return;

  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  if (isnan(temp)) temp = 0.0;
  if (isnan(umid)) umid = 0.0;

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/status");
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["temperatura"] = temp;
  doc["humidade"] = umid;
  doc["portaAberta"] = portaAberta;
  doc["fechaduraDestravada"] = fechaduraDestravada;
  doc["alarmeAtivo"] = alarmeAtivo;
  doc["motivoAlarme"] = motivoAlarme;
  doc["modoArmado"] = modoArmado;

  String payload;
  serializeJson(doc, payload);
  int httpCode = http.POST(payload);

  if (httpCode == 200) {
    String resposta = http.getString();
    JsonDocument resDoc;
    DeserializationError err = deserializeJson(resDoc, resposta);
    if (!err) {
      bool cmdLiberar = resDoc["comandoLiberar"] | false;
      bool novoModoArmado = resDoc["modoArmado"] | true;
      bool cmdSilenciar = resDoc["comandoSilenciar"] | false;

      modoArmado = novoModoArmado;

      if (cmdLiberar && !fechaduraDestravada) {
        acionarFechadura("Dashboard Local (Remoto)");
      }
      if (cmdSilenciar) {
        alarmeAtivo = false;
        motivoAlarme = "Nenhum";
        Serial.println("[ALARME] Alarme silenciado pelo operador.");
      }
    }
  }
  http.end();
}