// fingerprint_mod.cpp
#include "fingerprint_mod.h"

#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ===== Helpers de UI =====
static void showMsg(const String& s, unsigned long hold_ms = 0) {
  mensajeEnPantalla(s);
  if (hold_ms > 0) { waitMsWithLed(hold_ms); }
}

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

// ===================== Helpers locales (detección robusta) =====================
//
// Evita falsos positivos exigiendo varias lecturas consecutivas
//
static bool waitForFinger(unsigned long timeout_ms) {
  const int NEED_OK = 2;       // cuántos OK seguidos para confirmar dedo
  int okCount = 0;

  unsigned long t0 = millis();
  while ((millis() - t0) < timeout_ms) {
    int p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      if (++okCount >= NEED_OK) return true;
    } else if (p == FINGERPRINT_NOFINGER) {
      okCount = 0;
    }
    smartDelay(80);
  }
  return false;
}

static bool waitForNoFinger(unsigned long timeout_ms) {
  const int NEED_NO = 3;       // cuántos NOFINGER seguidos para confirmar retiro real
  int noCount = 0;

  unsigned long t0 = millis();
  while ((millis() - t0) < timeout_ms) {
    int p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      if (++noCount >= NEED_NO) return true;
    } else {
      noCount = 0;
    }
    smartDelay(60);
  }
  return false;
}

// ===================== API =====================

bool initFingerprintSensor(bool showLCD, uint8_t retries) {
  Serial.println("\nInicializando sensor de huellas...");
  if (showLCD) { mensajeEnPantalla("Init sensor"); indicarProcesando(); }

  sensorSerial.end();
  sensorSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  smartDelay(200);
  while (sensorSerial.available()) sensorSerial.read();

  // Intentos de conexión (verifyPassword bloquea ~1s si falla)
  for (int i = 0; i < retries; i++) {
    if (finger.verifyPassword()) {
      sensorReady = true;
      sensorRetryDelay = SENSOR_RETRY_MS;
      Serial.println("✓ Sensor conectado.");
      if (showLCD) { mensajeEnPantalla("Sensor OK"); indicarExito(); delay(800); }
      mostrarEstadisticasSensor();
      return true;
    }
    smartDelay(150);
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

  // Breve resumen en LCD
  mensajeEnPantalla("Cap:" + String(finger.capacity) + " Usadas:" + String(finger.templateCount));
  smartDelay(1000);
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
  if (!sensorReady) { showMsg("Sensor OFF", 1000); indicarFallo(); enterKeypadMode(); return; }

  // Dedo 1
  showMsg("Registro 1/2");
  if (!waitForFinger(10000)) { showMsg("Tiempo agotado", 1200); indicarFallo(); enterKeypadMode(); return; }
  if (finger.image2Tz(1) != FINGERPRINT_OK) { showMsg("Err img1", 1000); indicarFallo(); enterKeypadMode(); return; }

  // Retirar
  showMsg("Retira dedo");
  if (!waitForNoFinger(6000)) { showMsg("Retira dedo!", 1200); indicarFallo(); enterKeypadMode(); return; }

  // Dedo 2
  showMsg("Registro 2/2");
  if (!waitForFinger(10000)) { showMsg("Tiempo agotado", 1200); indicarFallo(); enterKeypadMode(); return; }
  if (finger.image2Tz(2) != FINGERPRINT_OK) { showMsg("Err img2", 1000); indicarFallo(); enterKeypadMode(); return; }

  // Modelo + guardar
  if (finger.createModel() != FINGERPRINT_OK) { showMsg("Err modelo", 1000); indicarFallo(); enterKeypadMode(); return; }

  if (finger.getTemplateCount() != FINGERPRINT_OK) { showMsg("Err count", 1000); indicarFallo(); enterKeypadMode(); return; }
  int newId = finger.templateCount + 1;

  if (finger.storeModel(newId) == FINGERPRINT_OK) {
    showMsg("Enrolado ID " + String(newId), 1200);
    indicarExito();
  } else {
    showMsg("Err guardar", 1200);
    indicarFallo();
  }
  enterKeypadMode();
}

// ---- Actualizar huella (robusto, con plan B) ----
void actualizarHuellaRemoto(int idHuella, int clienteId) {
  (void)clienteId; // reservado para futuro uso con backend
  const unsigned long TMO_COLOCAR_MS = 10000; // 10s para poner dedo
  const unsigned long TMO_RETIRAR_MS = 6000;  // 6s para retirarlo

  if (!sensorReady) {
    showMsg("Sensor OFF", 1000);
    indicarFallo();
    publishEvent("finger_update_error", "sensor_off");
    enterKeypadMode();
    return;
  }

  // Paso 1: dedo 1
  showMsg("Act ID " + String(idHuella) + " - dedo 1");
  if (!waitForFinger(TMO_COLOCAR_MS)) {
    showMsg("Tiempo agotado", 1200);
    indicarFallo();
    publishEvent("finger_update_timeout", "step1");
    enterKeypadMode();
    return;
  }
  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    showMsg("Err img1", 1000);
    indicarFallo();
    publishEvent("finger_update_error", "img1");
    enterKeypadMode();
    return;
  }

  // Paso 2: retirar dedo (robusto + plan B)
  showMsg("Retira dedo");
  bool retirado = waitForNoFinger(TMO_RETIRAR_MS);
  if (!retirado) {
    // Algunos sensores no llegan a NOFINGER estable: no bloqueamos.
    showMsg("Forzando...", 600);
    publishEvent("finger_update_warn", "retirar_forzado");
    // Pausa para evitar imagen idéntica en el segundo pase.
    smartDelay(800);
  }

  // Paso 3: dedo 2
  showMsg("Act ID " + String(idHuella) + " - dedo 2");
  if (!waitForFinger(TMO_COLOCAR_MS)) {
    showMsg("Tiempo agotado", 1200);
    indicarFallo();
    publishEvent("finger_update_timeout", "step2");
    enterKeypadMode();
    return;
  }
  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    showMsg("Err img2", 1000);
    indicarFallo();
    publishEvent("finger_update_error", "img2");
    enterKeypadMode();
    return;
  }

  // Paso 4: crear modelo y guardar
  if (finger.createModel() != FINGERPRINT_OK) {
    showMsg("Err modelo", 1000);
    indicarFallo();
    publishEvent("finger_update_error", "model");
    enterKeypadMode();
    return;
  }

  if (finger.storeModel(idHuella) == FINGERPRINT_OK) {
    showMsg("Huella actualizada", 900);
    indicarExito();
    publishEvent("finger_update_ok", nullptr);
  } else {
    showMsg("Err guardar", 1200);
    indicarFallo();
    publishEvent("finger_update_error", "store");
  }

  enterKeypadMode();
}

// ---- Borrar todas ----
void borrarTodasLasHuellas() {
  if (!sensorReady) { showMsg("Sensor OFF", 1000); indicarFallo(); enterKeypadMode(); return; }
  if (finger.emptyDatabase() == FINGERPRINT_OK) {
    showMsg("BD borrada", 1000);
    indicarExito();
    publishEvent("finger_db_cleared", nullptr);
  } else {
    showMsg("Err borrar BD", 1200);
    indicarFallo();
    publishEvent("finger_db_clear_error", nullptr);
  }
  enterKeypadMode();
}

// ---- Sincronizar (placeholder) ----
void sincronizarHuellasDesdeBackend() {
  showMsg("Sync (placeholder)", 800);
  Serial.println("Sincronizar: aquí va tu lógica de descarga Base64 y storeModel().");
  indicarProcesando(); waitMsWithLed(1000);
  publishEvent("finger_sync_placeholder", nullptr);
  enterKeypadMode();
}

// ---- Escaneo manual de huella (0#) ----
void iniciarEscaneoHuella() {
  if (!sensorReady) {
    showMsg("Sensor OFF", 1200);
    indicarFallo();
    enterKeypadMode();
    return;
  }

  showMsg("Coloca el dedo...");
  indicarProcesando();

  unsigned long t0 = millis();
  int p = finger.getImage();
  while (p == FINGERPRINT_NOFINGER && (millis() - t0) < 10000) {
    smartDelay(100);
    p = finger.getImage();
  }

  if (p != FINGERPRINT_OK) {
    showMsg("No hay dedo", 1200);
    indicarFallo();
    enterKeypadMode();
    return;
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    showMsg("Err procesar", 1200);
    indicarFallo();
    enterKeypadMode();
    return;
  }

  if (finger.fingerSearch() != FINGERPRINT_OK) {
    showMsg("No reconocida", 1200);
    indicarFallo();
    enterKeypadMode();
    return;
  }

  int foundId = finger.fingerID;
  int conf    = finger.confidence;
  if (conf < MIN_MATCH_CONFIDENCE) {
    showMsg("Conf baja", 1200);
    indicarFallo();
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
    if (rc == 200 || rc == 404) {
      String payload = http.getString();
      StaticJsonDocument<256> doc; DeserializationError e = deserializeJson(doc, payload);
      if (!e) {
        bool permitido = doc["permitido"] | false;
        const char* msg = doc["mensaje"];
        if (!msg) msg = doc["detail"];
        if (!msg) msg = (permitido ? "Acceso" : "Denegado");
        showMsg(String(msg));
        if (permitido) { indicarExito(); abrirPuerta(); publishEvent("access_fingerprint_ok", nullptr); }
        else { indicarFallo(); waitMsWithLed(ERROR_DISPLAY_MS); publishEvent("access_fingerprint_denied", nullptr); }
      } else {
        showMsg("Resp invalida", ERROR_DISPLAY_MS);
        indicarFallo();
      }
    } else {
      showMsg("Error servidor", ERROR_DISPLAY_MS);
      indicarFallo();
    }
    http.end();
  } else {
    showMsg("Sin WiFi", ERROR_DISPLAY_MS);
    indicarFallo();
  }

  enterKeypadMode();
}
