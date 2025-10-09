// fingerprint_mod.cpp
#include "fingerprint_mod.h"

#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===== Constantes/variables que vienen del .ino =====
extern const int FP_RX_PIN;
extern const int FP_TX_PIN;
extern const int MIN_MATCH_CONFIDENCE;
extern const char* SERVER_BASE_URL;

// ===== Estado sensor con reintento/backoff =====
bool sensorReady = false;
const unsigned long SENSOR_RETRY_MS  = 30000;
const unsigned long SENSOR_RETRY_MAX = 120000;
unsigned long sensorRetryDelay = SENSOR_RETRY_MS;
unsigned long nextSensorRetryAt = 0;

// ===== Instancias de hardware del sensor =====
HardwareSerial sensorSerial(2);                         // UART2 en ESP32
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&sensorSerial);

// ===================== Helpers locales (timeouts) =====================
static bool waitForFinger(unsigned long timeout_ms) {
  unsigned long t0 = millis();
  int p = finger.getImage();
  while (p == FINGERPRINT_NOFINGER && (millis() - t0) < timeout_ms) {
    delay(80);
    yield();
    p = finger.getImage();
  }
  return (p == FINGERPRINT_OK);
}

static bool waitForNoFinger(unsigned long timeout_ms) {
  unsigned long t0 = millis();
  while (finger.getImage() != FINGERPRINT_NOFINGER && (millis() - t0) < timeout_ms) {
    delay(60);
    yield();
  }
  return (millis() - t0) < timeout_ms;
}

// ===================== API =====================

bool initFingerprintSensor(bool showLCD) {
  Serial.println("\nInicializando sensor de huellas...");
  if (showLCD) { mensajeEnPantalla("Init sensor"); indicarProcesando(); }

  sensorSerial.end();
  sensorSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  delay(200);
  while (sensorSerial.available()) sensorSerial.read();

  for (int i = 0; i < 3; i++) {
    if (finger.verifyPassword()) {
      sensorReady = true;
      sensorRetryDelay = SENSOR_RETRY_MS;
      Serial.println("✓ Sensor conectado.");
      if (showLCD) { mensajeEnPantalla("Sensor OK"); indicarExito(); delay(800); }
      mostrarEstadisticasSensor();
      return true;
    }
    delay(150);
  }

  sensorReady = false;
  Serial.println("✗ No se pudo conectar al sensor.");
  if (showLCD) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); }
  scheduleSensorRetry(sensorRetryDelay);
  return false;
}

void scheduleSensorRetry(unsigned long ms) {
  nextSensorRetryAt = millis() + ms;
}

void mostrarEstadisticasSensor() {
  if (!sensorReady) { Serial.println("Sensor OFF: sin stats."); return; }

  int p = finger.getParameters();
  if (p == FINGERPRINT_OK) {
    Serial.printf("🔢 Capacidad: %u\n", finger.capacity);
  } else {
    Serial.printf("⚠️ No params (code=%d)\n", p);
  }

  p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK) {
    Serial.printf("📦 Plantillas: %u\n", finger.templateCount);
  } else {
    Serial.printf("⚠️ No count (code=%d)\n", p);
  }

  // Breve resumen en LCD (vía módulo display)
  mensajeEnPantalla("Cap:" + String(finger.capacity) + " Usadas:" + String(finger.templateCount));
  delay(1000);
}

// ---- Verificar por huella (placeholder guiado) ----
void verificarAcceso() {
  mensajeEnPantalla("Use 0# para escanear");
  indicarProcesando(); waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Identificar Usuario (placeholder guiado) ----
void identificarUsuario() {
  mensajeEnPantalla("Use 0# para escanear");
  indicarProcesando(); waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Enrolar (con timeouts) ----
void enrolarNuevoUsuario() {
  if (!sensorReady) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  // Dedo 1
  mensajeEnPantalla("Registro 1/2");
  if (!waitForFinger(10000)) { mensajeEnPantalla("Tiempo agotado"); indicarFallo(); waitMsWithLed(1200); enterKeypadMode(); return; }
  if (finger.image2Tz(1) != FINGERPRINT_OK) { mensajeEnPantalla("Err img1"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  // Retirar
  mensajeEnPantalla("Retira dedo");
  if (!waitForNoFinger(6000)) { mensajeEnPantalla("Retira dedo!"); indicarFallo(); waitMsWithLed(1200); enterKeypadMode(); return; }

  // Dedo 2
  mensajeEnPantalla("Registro 2/2");
  if (!waitForFinger(10000)) { mensajeEnPantalla("Tiempo agotado"); indicarFallo(); waitMsWithLed(1200); enterKeypadMode(); return; }
  if (finger.image2Tz(2) != FINGERPRINT_OK) { mensajeEnPantalla("Err img2"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  // Modelo + guardar
  if (finger.createModel() != FINGERPRINT_OK) { mensajeEnPantalla("Err modelo"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  if (finger.getTemplateCount() != FINGERPRINT_OK) { mensajeEnPantalla("Err count"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }
  int newId = finger.templateCount + 1;

  if (finger.storeModel(newId) == FINGERPRINT_OK) {
    mensajeEnPantalla("Enrolado ID " + String(newId));
    indicarExito();
  } else {
    mensajeEnPantalla("Err guardar");
    indicarFallo();
  }
  waitMsWithLed(1200);
  enterKeypadMode();
}

// ---- Actualizar huella (con timeouts y eventos) ----
void actualizarHuellaRemoto(int idHuella, int clienteId) {
  (void)clienteId; // por si luego lo usas en backend
  const unsigned long TMO_COLOCAR = 10000; // 10s para poner dedo
  const unsigned long TMO_RETIRAR = 6000;  // 6s para retirarlo

  if (!sensorReady) {
    mensajeEnPantalla("Sensor OFF");
    indicarFallo(); waitMsWithLed(1000);
    publishEvent("finger_update_error", "sensor_off");
    enterKeypadMode();
    return;
  }

  // Paso 1: dedo 1
  mensajeEnPantalla("Act ID " + String(idHuella) + " - dedo 1");
  if (!waitForFinger(TMO_COLOCAR)) {
    mensajeEnPantalla("Tiempo agotado");
    indicarFallo(); waitMsWithLed(1200);
    publishEvent("finger_update_timeout", "step1");
    enterKeypadMode();
    return;
  }
  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    mensajeEnPantalla("Err img1");
    indicarFallo(); waitMsWithLed(1000);
    publishEvent("finger_update_error", "img1");
    enterKeypadMode();
    return;
  }

  // Paso 2: retirar dedo
  mensajeEnPantalla("Retira dedo");
  if (!waitForNoFinger(TMO_RETIRAR)) {
    mensajeEnPantalla("Retira dedo!");
    indicarFallo(); waitMsWithLed(1200);
    publishEvent("finger_update_timeout", "retirar");
    enterKeypadMode();
    return;
  }

  // Paso 3: dedo 2
  mensajeEnPantalla("Act ID " + String(idHuella) + " - dedo 2");
  if (!waitForFinger(TMO_COLOCAR)) {
    mensajeEnPantalla("Tiempo agotado");
    indicarFallo(); waitMsWithLed(1200);
    publishEvent("finger_update_timeout", "step2");
    enterKeypadMode();
    return;
  }
  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    mensajeEnPantalla("Err img2");
    indicarFallo(); waitMsWithLed(1000);
    publishEvent("finger_update_error", "img2");
    enterKeypadMode();
    return;
  }

  // Paso 4: crear modelo y guardar
  if (finger.createModel() != FINGERPRINT_OK) {
    mensajeEnPantalla("Err modelo");
    indicarFallo(); waitMsWithLed(1000);
    publishEvent("finger_update_error", "model");
    enterKeypadMode();
    return;
  }

  if (finger.storeModel(idHuella) == FINGERPRINT_OK) {
    mensajeEnPantalla("Huella actualizada");
    indicarExito(); waitMsWithLed(800);
    publishEvent("finger_update_ok", nullptr);
  } else {
    mensajeEnPantalla("Err guardar");
    indicarFallo(); waitMsWithLed(1200);
    publishEvent("finger_update_error", "store");
  }

  enterKeypadMode();
}

// ---- Borrar todas ----
void borrarTodasLasHuellas() {
  if (!sensorReady) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }
  if (finger.emptyDatabase() == FINGERPRINT_OK) {
    mensajeEnPantalla("BD borrada");
    indicarExito();
    publishEvent("finger_db_cleared", nullptr);
  } else {
    mensajeEnPantalla("Err borrar BD");
    indicarFallo();
    publishEvent("finger_db_clear_error", nullptr);
  }
  waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Sincronizar (placeholder) ----
void sincronizarHuellasDesdeBackend() {
  mensajeEnPantalla("Sync (placeholder)");
  Serial.println("Sincronizar: aquí va tu lógica de descarga Base64 y storeModel().");
  indicarProcesando(); waitMsWithLed(1000);
  publishEvent("finger_sync_placeholder", nullptr);
  enterKeypadMode();
}

// ---- Escaneo manual de huella (0#) ----
void iniciarEscaneoHuella() {
  if (!sensorReady) {
    mensajeEnPantalla("Sensor OFF");
    indicarFallo(); waitMsWithLed(1200);
    enterKeypadMode();
    return;
  }

  mensajeEnPantalla("Coloca el dedo...");
  indicarProcesando();

  int p = finger.getImage();
  unsigned long t0 = millis();
  while (p == FINGERPRINT_NOFINGER && (millis() - t0) < 10000) {
    delay(100);
    p = finger.getImage();
  }

  if (p != FINGERPRINT_OK) {
    mensajeEnPantalla("No hay dedo");
    indicarFallo(); waitMsWithLed(1200);
    enterKeypadMode();
    return;
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    mensajeEnPantalla("Err procesar");
    indicarFallo(); waitMsWithLed(1200);
    enterKeypadMode();
    return;
  }

  if (finger.fingerSearch() != FINGERPRINT_OK) {
    mensajeEnPantalla("No reconocida");
    indicarFallo(); waitMsWithLed(1200);
    enterKeypadMode();
    return;
  }

  int foundId = finger.fingerID;
  int conf    = finger.confidence;
  if (conf < MIN_MATCH_CONFIDENCE) {
    mensajeEnPantalla("Conf baja");
    indicarFallo(); waitMsWithLed(1200);
    enterKeypadMode();
    return;
  }

  // Validación con backend
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String accesoUrl = String(SERVER_BASE_URL) + "acceso/verificar-acceso";
    http.setTimeout(5000);
    http.begin(accesoUrl);
    http.addHeader("Content-Type", "application/json");
    String body = String("{\"id_huella\":") + String(foundId) + "}";
    int rc = http.POST(body);
    if (rc == 200) {
      String payload = http.getString();
      StaticJsonDocument<256> doc; DeserializationError e = deserializeJson(doc, payload);
      if (!e) {
        bool permitido = doc["permitido"] | false;
        const char* msg = doc["mensaje"] | (permitido ? "Acceso" : "Denegado");
        mensajeEnPantalla(String(msg));
        if (permitido) { indicarExito(); abrirPuerta(); publishEvent("access_fingerprint_ok", nullptr); }
        else { indicarFallo(); waitMsWithLed(1200); publishEvent("access_fingerprint_denied", nullptr); }
      } else {
        mensajeEnPantalla("Resp invalida");
        indicarFallo(); waitMsWithLed(1200);
      }
    } else {
      mensajeEnPantalla("Error servidor");
      indicarFallo(); waitMsWithLed(1200);
    }
    http.end();
  } else {
    mensajeEnPantalla("Sin WiFi");
    indicarFallo(); waitMsWithLed(1200);
  }

  enterKeypadMode();
}
