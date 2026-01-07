// =========================== SENSOR HUELLA + TECLADO WIEGAND ESP32 ===========================
// Requisitos: ESP32 Arduino core 3.x, librerías: Adafruit_Fingerprint, Wiegand, ESP32Servo,
//             ArduinoJson, LiquidCrystal_I2C (Rickman/Brabander), WiFi, HTTPClient, PubSubClient.
// =============================================================================================

#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "base64.h"
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Wiegand.h>
#include <Arduino.h>
#include <Wire.h>
#include <PubSubClient.h>

// === Configuración (pines, credenciales, tiempos, etc.) ===
#include "config.h"

// === Módulos separados ===
#include "display_mod.h"     // ⬅️ NUEVO: UI/LCD
#include "fingerprint_mod.h"
#include "leds_mod.h"
#include "mqtt_mod.h"
#include "door_mod.h"

// -----------------------------------------------------------------------------
// -- ⚙️ ESTADO / OBJETOS GLOBALES (no-config)
// -----------------------------------------------------------------------------

#include <Preferences.h>

// -----------------------------------------------------------------------------

// LED (enums definidos en leds_mod.h)
LedMode     ledMode       = MODE_STATIC;
FixedKind   fixedKind     = FIX_GREEN;
unsigned long feedbackUntil = 0;
// Color inicial (Gold por defecto, o apagado si prefieres) -> Gold (255, 220, 4)
uint8_t currentR = 255;
uint8_t currentG = 220;
uint8_t currentB = 4;

Preferences preferences;

void saveLedColor(uint8_t r, uint8_t g, uint8_t b) {
  currentR = r;
  currentG = g;
  currentB = b;
  setColor(r, g, b);
  
  preferences.begin("config", false); // Namespace "config", RW
  preferences.putUChar("led_r", r);
  preferences.putUChar("led_g", g);
  preferences.putUChar("led_b", b);
  preferences.end();
  
  Serial.println("💾 Config RGB guardada en NVS.");
}

// Wiegand (teclado)
#define PIN_D0 4
#define PIN_D1 5
WIEGAND wg;
unsigned long lastKeyTime = 0;

// Teclado buffer/lock
String cedulaBuffer = "";
bool   keypadLocked = false;

// MQTT client (transport + client)
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// Topics MQTT (derivados de SEDE/DEV en config.h)
String T_CMD    = String("devices/") + SEDE + "/" + DEV + "/cmd";
String T_ACK    = String("devices/") + SEDE + "/" + DEV + "/cmd/ack";
String T_STATE  = String("devices/") + SEDE + "/" + DEV + "/state";
String T_EVENT  = String("devices/") + SEDE + "/" + DEV + "/event";
String T_CONFIG = String("devices/") + SEDE + "/" + DEV + "/config";

// Desactiva el polling HTTP de comandos (usa MQTT cmd/ack). Pon 1 si quieres mantenerlo.
#define USE_HTTP_POLLING 0

// -----------------------------------------------------------------------------
// -- PROTOTIPOS (solo funciones locales del .ino)
// -----------------------------------------------------------------------------
void conectarWiFi();
void confirmarComando(String comando);
bool abrirPuerta();

void indicarExito();
void indicarFallo();
void indicarProcesando();
void apagarLeds();

void enterKeypadMode();
void renderCedulaBuffer();
void handleWiegand();
void verificarCedulaYAccionar(const String& cedula);

// Helpers teclado
inline bool isHash(uint64_t code){ return (code==13 || code==35 || code==11); } // '#'
inline bool isStar(uint64_t code){ return (code==27 || code==42 || code==10); } // '*'

// -----------------------------------------------------------------------------
// -- SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  // UI (LCD autodetect)
  displayBegin();

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  ledMode = MODE_STATIC;

  // Servo (módulo puerta)
  doorBegin(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE, SERVO_CLOSED_ANGLE, SERVO_OPEN_ANGLE);
  
  // Cargar color guardado
  preferences.begin("config", true); // RO mode
  currentR = preferences.getUChar("led_r", 255);
  currentG = preferences.getUChar("led_g", 220);
  currentB = preferences.getUChar("led_b", 4);
  preferences.end();
  Serial.printf("🌈 Color inicial cargado: %d, %d, %d\n", currentR, currentG, currentB);

  Serial.println("\nIniciando Wiegand...");
  wg.begin(PIN_D0, PIN_D1);
  Serial.println("Listo. Digita en el teclado.");

  mensajeEnPantalla("Conectando WIFI");
  indicarProcesando();
  conectarWiFi();

  // MQTT
  ensureMqttConnected();

  // Huellas (módulo)
  initFingerprintSensor(true);

  enterKeypadMode();

  randomSeed(esp_random());
}

// -----------------------------------------------------------------------------
// -- LOOP
// -----------------------------------------------------------------------------
void loop() {
  // MQTT
  if (!mqtt.connected()) ensureMqttConnected();
  mqtt.loop();

  // LEDs
  updateLed();

  // Reintento sensor (variables del módulo fingerprint)
  if (!sensorReady && (long)(millis() - nextSensorRetryAt) >= 0) {
    bool ok = initFingerprintSensor(false);
    if (!ok) {
      sensorRetryDelay = min(sensorRetryDelay * 2, SENSOR_RETRY_MAX);
      scheduleSensorRetry(sensorRetryDelay);
    }
  }

  // Teclado siempre activo
  handleWiegand();

#if USE_HTTP_POLLING
  // --- Polling de comandos del servidor (HTTP) ---
  if (keypadLocked) {
    if (cedulaBuffer.length() == 0) {
      keypadLocked = false;
      enterKeypadMode();
    } else if ((millis() - lastKeyTime) > CEDULA_TIMEOUT_MS) {
      Serial.println("⌛ Timeout teclado: limpiar/desbloquear.");
      cedulaBuffer = "";
      keypadLocked = false;
      enterKeypadMode();
    }
    return;
  }

  static unsigned long nextPollAt = 0;
  unsigned long now = millis();
  if ((long)(now - nextPollAt) >= 0) {
    nextPollAt = now + POLLING_INTERVAL_MS + (uint32_t)random(0, 800);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi caído, reconectando...");
      conectarWiFi();
    }

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = String(SERVER_BASE_URL) + "dispositivo/dispositivo/" + String(DEVICE_ID) + "/comando";
      Serial.println("⏳ Consultando: " + url);
      http.setTimeout(4000);
      http.begin(url);
      int httpResponseCode = http.GET();

      if (httpResponseCode == 200) {
        String payload = http.getString();
        Serial.println("📩 Respuesta: " + payload);

        if (payload != "{}") {
          StaticJsonDocument<256> doc;
          DeserializationError error = deserializeJson(doc, payload);
          if (!error) {
            const char* comando = doc["comando"];
            int clienteId = doc["cliente_id"] | 0;
            int idHuella  = doc["id_huella"]  | 0;

            apagarLeds();
            if (strcmp(comando, "update") == 0 && idHuella > 0 && clienteId > 0) {
              actualizarHuellaRemoto(idHuella, clienteId);
              confirmarComando("update");
            } else if (strcmp(comando, "enroll") == 0) {
              enrolarNuevoUsuario();
              confirmarComando("enroll");
            } else if (strcmp(comando, "verify") == 0) {
              mensajeEnPantalla("Use 0# para escanear");
              indicarProcesando(); waitMsWithLed(1200);
              confirmarComando("verify_skipped");
            } else if (strcmp(comando, "search") == 0) {
              mensajeEnPantalla("Use 0# para escanear");
              indicarProcesando(); waitMsWithLed(1200);
              confirmarComando("search_skipped");
            } else if (strcmp(comando, "open") == 0) {
              bool ok = abrirPuerta();
              confirmarComando("open");
              (void)ok;
            } else if (strcmp(comando, "delete") == 0) {
              borrarTodasLasHuellas();
              confirmarComando("delete");
            } else if (strcmp(comando, "sync") == 0) {
              sincronizarHuellasDesdeBackend();
              confirmarComando("sync");
            } else {
              Serial.println("⚠️ Comando desconocido");
            }
            enterKeypadMode(); // ya redibuja
          } else {
            Serial.println("⚠️ Error parseando JSON");
          }
        } else {
          Serial.println("ℹ️ Sin comandos.");
        }
      } else {
        Serial.printf("❌ Error HTTP: %d\n", httpResponseCode);
      }
      http.end();
    }
  }
#endif
}

// -----------------------------------------------------------------------------
// -- TECLADO WIEGAND / MODO CÉDULA
// -----------------------------------------------------------------------------
void enterKeypadMode() {
  cedulaBuffer = "";
  keypadLocked = false;
  mensajeEnPantalla("Digita tu cedula");
}

void renderCedulaBuffer() {
  // Línea 1 fija, línea 2: últimos 16 chars del buffer
  String l2 = (cedulaBuffer.length() <= 16)
              ? cedulaBuffer
              : cedulaBuffer.substring(cedulaBuffer.length() - 16);
  displayShowTwoLines("Digita tu cedula", l2);
}

// Procesa un código (sea de Wiegand o simulado por Serial)
void processKeyCode(uint64_t code) {
  lastKeyTime = millis();

  // Comienza entrada → bloquear
  if (!keypadLocked && (code <= 9 || (code >= 48 && code <= 57))) {
    keypadLocked = true;
    Serial.println("🔒 Teclado activo (bloqueo polling).");
    mensajeEnPantalla("Modo Teclado");
  }

  // *
  if (isStar(code)) {
    cedulaBuffer = "";
    keypadLocked = false;
    Serial.println("✖ Borrado por '*', desbloqueado.");
    renderCedulaBuffer();
    return;
  }

  // #
  if (isHash(code)) {
    if (cedulaBuffer.length() == 0) {
      mensajeEnPantalla("Buffer vacio");
      indicarFallo(); waitMsWithLed(800); enterKeypadMode();
      keypadLocked = false;
      return;
    }

    // Secreto -> abrir
    if (cedulaBuffer == String(SECRET_CODE)) {
      Serial.println("🔐 Codigo secreto -> abrirPuerta");
      mensajeEnPantalla("Codigo secreto");
      indicarExito();
      abrirPuerta();
      cedulaBuffer = "";
      keypadLocked = false;
      enterKeypadMode();
      return;
    }

    // 0# → escaneo por huella
    if (cedulaBuffer == "0") {
      Serial.println("👉 0# detectado: iniciar escaneo de huella");
      cedulaBuffer = "";
      keypadLocked = false;
      iniciarEscaneoHuella(); // módulo de huellas
      return;
    }

    // Verificar cédula normal (HTTP)
    verificarCedulaYAccionar(cedulaBuffer);
    keypadLocked = false;
    return;
  }

  // Dígitos
  if (code <= 9) cedulaBuffer += char('0' + (uint8_t)code);
  else if (code >= 48 && code <= 57) cedulaBuffer += char(code);
  else Serial.printf("Tecla desconocida: %llu\n", (unsigned long long)code);

  renderCedulaBuffer();
}

// --- Buffer global para Serial ---
String serialInputBuffer = "";

void processSerialCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  // 1. Comandos de Debug (empiezan con '!')
  if (line.startsWith("!")) {
    Serial.println("🔧 COMANDO DEBUG: " + line);
    
    // !update <id> <cliente>
    if (line.startsWith("!update ")) {
      // Parsear argumentos simples
      int firstSpace = line.indexOf(' ');
      int secondSpace = line.indexOf(' ', firstSpace + 1);
      
      if (firstSpace > 0 && secondSpace > 0) {
        int idHuella = line.substring(firstSpace + 1, secondSpace).toInt();
        int clienteId = line.substring(secondSpace + 1).toInt();
        Serial.printf("➡️ Ejecutando actualizarHuellaRemoto(%d, %d)\n", idHuella, clienteId);
        actualizarHuellaRemoto(idHuella, clienteId);
      } else {
        Serial.println("❌ Uso: !update <id_huella> <cliente_id>");
      }
    } 
    else if (line == "!enroll") {
      Serial.println("➡️ Ejecutando enrolarNuevoUsuario()");
      enrolarNuevoUsuario();
    }
    else if (line == "!delete") {
      Serial.println("➡️ Ejecutando borrarTodasLasHuellas()");
      borrarTodasLasHuellas();
    }
    else if (line == "!info") {
      mostrarEstadisticasSensor();
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.println("📡 MQTT Topics:");
      Serial.println("   CMD: " + T_CMD);
    }
    else {
      Serial.println("⚠️ Comando desconocido. Disponibles: !update, !enroll, !delete, !info");
    }
    return;
  }

  // 2. Si no es comando, es simulación de teclado (envía toda la cadena carácter por carácter)
  Serial.println("⌨️ Simulación Teclado (Batch): " + line);
  for (unsigned int i = 0; i < line.length(); i++) {
    char c = line.charAt(i);
    processKeyCode((uint64_t)c);
    delay(50); // Pequeña pausa para 'sentir' el tecleo
  }
}

void handleWiegand() {
  // 1. Entrada física Wiegand
  if (wg.available()) {
    processKeyCode(wg.getCode());
  }

  // 2. Simulación por Serial (Buffering completo)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInputBuffer.length() > 0) {
        processSerialCommand(serialInputBuffer);
        serialInputBuffer = "";
      }
    } else {
      serialInputBuffer += c;
    }
  }

  // Timeout
  if (cedulaBuffer.length() > 0 && (millis() - lastKeyTime > CEDULA_TIMEOUT_MS)) {
    Serial.println("⌛ Timeout teclado: limpiar y desbloquear.");
    cedulaBuffer = "";
    keypadLocked = false;
    enterKeypadMode();
  }
}

void verificarCedulaYAccionar(const String& cedula) {
  if (cedula.length() == 0) {
    mensajeEnPantalla("Cedula vacia");
    indicarFallo(); waitMsWithLed(1500); enterKeypadMode();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    mensajeEnPantalla("Sin WiFi");
    indicarFallo(); waitMsWithLed(1500); enterKeypadMode();
    return;
  }

  HTTPClient http;
  String accesoUrl = String(SERVER_BASE_URL) + "acceso/verificar-acceso";
  http.setTimeout(6000);
  http.begin(accesoUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<96> bodyDoc;
  bodyDoc["documento"] = cedula;
  String body; serializeJson(bodyDoc, body);

  mensajeEnPantalla("Verificando...");
  int httpResponseCode = http.POST(body);

  if (httpResponseCode == 200 || httpResponseCode == 404) {
    String payload = http.getString();
    StaticJsonDocument<256> resp;
    DeserializationError err = deserializeJson(resp, payload);
    if (!err) {
      bool permitido = resp["permitido"] | false;
      const char* mensaje = resp["mensaje"];
      if (!mensaje) mensaje = resp["detail"];
      if (!mensaje) mensaje = (permitido ? "Acceso" : "Denegado");
      mensajeEnPantalla(String(mensaje));

      if (permitido) {
        indicarExito();
        bool okOpen = abrirPuerta();
        (void)okOpen;
        publishEvent("access_card_ok", nullptr);
      } else {
        // En 404 también cae aquí si 'permitido' es false
        indicarFallo(); waitMsWithLed(ERROR_DISPLAY_MS);
        publishEvent("access_card_denied", nullptr);
      }
    } else {
      mensajeEnPantalla("Error respuesta");
      indicarFallo(); waitMsWithLed(ERROR_DISPLAY_MS);
    }
  } else {
    mensajeEnPantalla("Error servidor");
    indicarFallo(); waitMsWithLed(ERROR_DISPLAY_MS);
  }
  http.end();
  enterKeypadMode();
}

// -----------------------------------------------------------------------------
// -- IMPLEMENTACIONES REQUERIDAS
// -----------------------------------------------------------------------------
void conectarWiFi() {
  Serial.print("Conectando a WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(300); Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi conectado");
    mensajeEnPantalla("WIFI Conectado");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    indicarExito();
  } else {
    Serial.println("\n❌ Fallo WiFi. Reinicio...");
    mensajeEnPantalla("Error WIFI");
    indicarFallo(); delay(1200); ESP.restart();
  }
}

void confirmarComando(String comando) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(SERVER_BASE_URL) + "dispositivo/dispositivo/" + String(DEVICE_ID) + "/confirmar";
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<128> doc; doc["comando"] = comando;
  String body; serializeJson(doc, body);
  int rc = http.POST(body);
  Serial.printf("Confirmar '%s' -> HTTP %d\n", comando.c_str(), rc);
  http.end();
}

// Wrapper: módulo Door
bool abrirPuerta() {
  if (!doorIsAttached()) {
    mensajeEnPantalla("Error servo");
    indicarFallo();
    return false;
  }

  auto onOpenStart = []() {
    triggerFeedback(FIX_GREEN, DOOR_OPEN_TIME_MS + DOOR_CLOSE_DELAY_MS + 500);
    mensajeEnPantalla("Bienvenido");
  };
  auto onCloseStart = []() {
    mensajeEnPantalla("Cerrando puerta");
  };
  auto onDone = []() {
    ledMode = MODE_STATIC;
    keypadLocked = false;
    enterKeypadMode();
  };
  auto tick = []() { updateLed(); };

  return doorOpenAndClose(DOOR_OPEN_TIME_MS, DOOR_CLOSE_DELAY_MS,
                          onOpenStart, onCloseStart, onDone, tick);
}

// LED helpers
void indicarExito(){ triggerFeedback(FIX_GREEN, 1800); }
void indicarFallo(){ triggerFeedback(FIX_RED,   1800); }
void indicarProcesando(){ ledMode = MODE_STATIC; }
void apagarLeds(){ setColor(0,0,0); }

void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  while ((millis() - start) < ms) {
    if (mqtt.connected()) mqtt.loop(); 
    delay(1);
  }
}
