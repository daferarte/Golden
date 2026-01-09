// =========================== SENSOR HUELLA + TECLADO WIEGAND ESP32 ===========================
// Requisitos: ESP32 Arduino core 3.x, librerías: Adafruit_Fingerprint, Wiegand, ESP32Servo,
//             ArduinoJson, LiquidCrystal_I2C (Rickman/Brabander), WiFi, HTTPClient, PubSubClient.
// =============================================================================================

#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include "soc/soc.h"           // Brownout fix
#include "soc/rtc_cntl_reg.h"  // Brownout fix
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
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Desactivar detector de caida de voltaje (Brownout)
  setCpuFrequencyMhz(240); // Forzar máximo rendimiento CPU (240MHz)
  Serial.begin(115200);
  delay(100);

  Serial.println("\n=== INICIO SISTEMA ===");

  // 1. Iniciar Wiegand PRIMERO (Prioridad Hardware)
  Serial.println("Iniciando Wiegand...");
  pinMode(PIN_D0, INPUT_PULLUP); // Refuerzo eléctrico
  pinMode(PIN_D1, INPUT_PULLUP);
  wg.begin(PIN_D0, PIN_D1);
  Serial.println("✅ Wiegand listo. Digita en cualquier momento.");

  // UI (LCD autodetect)
  displayBegin();

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  ledMode = MODE_STATIC;

  // Servo (módulo puerta)
  doorBegin(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE, SERVO_CLOSED_ANGLE, SERVO_OPEN_ANGLE);
  // Asegurar posición cerrada al inicio
  delay(500); 
  Serial.println("🚪 Puerta inicializada en CERRADO.");
  
  // Cargar color
  preferences.begin("config", true);
  currentR = preferences.getUChar("led_r", 255);
  currentG = preferences.getUChar("led_g", 220);
  currentB = preferences.getUChar("led_b", 4);
  preferences.end();
  Serial.printf("🌈 Color cargado: %d, %d, %d\n", currentR, currentG, currentB);

  // WiFi con teclado activo
  mensajeEnPantalla("Conectando WIFI");
  indicarProcesando();
  conectarWiFi();

  // MQTT
  // MQTT - Connection handled in loop (non-blocking)
  Serial.println("🔌 Conectando MQTT inicial (bloqueante)...");
  blockingMqttConnect(); 
  // blockingMqttConnect(); // REMOVED to prioritize Keypad startup

  // Huellas (módulo)
  initFingerprintSensor(true);

  enterKeypadMode();

  randomSeed(esp_random());
}

// -----------------------------------------------------------------------------
// -- LOOP
// -----------------------------------------------------------------------------
void loop() {
  // 1. PRIORIDAD ABSOLUTA: Teclado
  handleWiegand();

  // 2. Bloqueo de Tareas de Fondo
  // Si hay flag de bloqueo O si hay algo en el buffer, asumimos que el usuario está escribiendo.
  // Esto previene que MQTT interfiera mientras se digita.
  if (keypadLocked || cedulaBuffer.length() > 0) {
     
     // Revisar timeout
     if (cedulaBuffer.length() > 0 && (millis() - lastKeyTime > CEDULA_TIMEOUT_MS)) {
        Serial.println("⌛ Timeout teclado: limpiar/desbloquear.");
        cedulaBuffer = "";
        keypadLocked = false;
        enterKeypadMode();
     }
     
     // Retornamos inmediato para NO ejecutar MQTT ni nada más
     return;
  }

  nonBlockingMqttLoop();

  

  
}

// -----------------------------------------------------------------------------
// -- TECLADO WIEGAND / MODO CÉDULA
// -----------------------------------------------------------------------------
void enterKeypadMode() {
  cedulaBuffer = "";
  keypadLocked = false;
  mensajeEnPantalla("Digita tu cedula");
  setColor(currentR, currentG, currentB); // Restaurar color de reposo
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
    mensajeEnPantalla("Modo Teclado");
  }

  // *
  if (isStar(code)) {
    cedulaBuffer = "";
    keypadLocked = false;
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
      mensajeEnPantalla("Codigo secreto");
      abrirPuerta();
      cedulaBuffer = "";
      keypadLocked = false;
      enterKeypadMode();
      return;
    }

    // 0# → escaneo por huella
    if (cedulaBuffer == "0") {
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
  if (code <= 9) {
    cedulaBuffer += char('0' + (uint8_t)code);
    keypadLocked = true;
  }
  else if (code >= 48 && code <= 57) {
    cedulaBuffer += char(code);
    keypadLocked = true;
  }
  
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
    // Serial.println("⌨️ KEY"); // Log desactivado por rendimiento
    processKeyCode(wg.getCode());
  }

  /* SIMULACIÓN DESACTIVADA POR RENDIMIENTO
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
  */

  // Timeout manejado en loop() para centralizar
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
        // indicarExito(); // Quitamos para evitar delay extra antes de abrir
        bool okOpen = abrirPuerta(); // Maneja su propio verde y texto
        (void)okOpen;
        publishEvent("access_card_ok", nullptr);
      } else {
        // En 404 también cae aquí si 'permitido' es false
        indicarFallo(); waitMsWithLed(ERROR_DISPLAY_MS);
        setColor(currentR, currentG, currentB); // Restaurar
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
  
  // IMPORTANTE: Configuración para evitar latencia y desconexiones con fuente externa
  WiFi.mode(WIFI_STA); 
  WiFi.setSleep(false); // Desactiva ahorro de energía (radio siempre activa)
  
  WiFi.begin(ssid, password);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    smartDelay(300); Serial.print(".");
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

  // Callbacks para LEDs manuales (sin updateLed en loop)
  auto onOpenStart = []() {
    setColor(0, 255, 0); // VERDE directo
    mensajeEnPantalla("Bienvenido");
  };
  auto onCloseStart = []() {
    mensajeEnPantalla("Cerrando puerta");
  };
  auto onDone = []() {
    setColor(currentR, currentG, currentB); // Restaurar color de reposo
    keypadLocked = false;
    enterKeypadMode();
  };
  // Tick vacío, ya no usamos updateLed()
  auto tick = []() { };

  return doorOpenAndClose(DOOR_OPEN_TIME_MS, DOOR_CLOSE_DELAY_MS,
                          onOpenStart, onCloseStart, onDone, tick);
}

// LED helpers
// LED helpers con delay bloqueante (pero con smartDelay para teclado)
void indicarExito(){ 
  setColor(0, 255, 0); // Verde
  smartDelay(1000);    // Feedback visible
}
void indicarFallo(){ 
  setColor(255, 0, 0); // Rojo
  smartDelay(1000);    // Feedback visible
}
void indicarProcesando(){ setColor(currentR, currentG, currentB); } 
void apagarLeds(){ setColor(0,0,0); }

// smartDelay desactivado (se comporta como un delay normal)
void smartDelay(unsigned long ms) {
  delay(ms); 
}
