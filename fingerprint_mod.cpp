// fingerprint_mod.cpp
#include "fingerprint_mod.h"

#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===== Variables/constantes que declaraste en el .ino =====
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
HardwareSerial sensorSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&sensorSerial);

// ===================== FUNCIONES =====================

bool initFingerprintSensor(bool showLCD) {
  Serial.println("\nInicializando sensor de huellas...");
  if (showLCD) { mensajeEnPantalla("Init sensor"); indicarProcesando(); }

  sensorSerial.end();
  sensorSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  delay(200);
  while (sensorSerial.available()) sensorSerial.read();

  for (int i=0; i<3; i++) {
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
  if (p == FINGERPRINT_OK) Serial.printf("🔢 Capacidad: %u\n", finger.capacity);
  else Serial.printf("⚠️ No params (code=%d)\n", p);

  p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK) Serial.printf("📦 Plantillas: %u\n", finger.templateCount);
  else Serial.printf("⚠️ No count (code=%d)\n", p);

  // Mostrar un resumen en la pantalla usando la API del módulo display
  String msg = "Cap:" + String(finger.capacity) + " Usadas:" + String(finger.templateCount);
  mensajeEnPantalla(msg);
  delay(1000);
}

// ---- Verificar por huella (compacto) ----
void verificarAcceso() {
  mensajeEnPantalla("Use 0# para escanear");
  indicarProcesando(); waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Identificar Usuario (GET datos) ----
void identificarUsuario() {
  mensajeEnPantalla("Use 0# para escanear");
  indicarProcesando(); waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Enrolar (resumido) ----
void enrolarNuevoUsuario() {
  if (!sensorReady) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  mensajeEnPantalla("Registro 1/2");
  while (finger.getImage() != FINGERPRINT_OK) { delay(80); }
  if (finger.image2Tz(1) != FINGERPRINT_OK) { mensajeEnPantalla("Err img1"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  mensajeEnPantalla("Retira dedo"); delay(800);
  while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(80); }

  mensajeEnPantalla("Registro 2/2");
  while (finger.getImage() != FINGERPRINT_OK) { delay(80); }
  if (finger.image2Tz(2) != FINGERPRINT_OK) { mensajeEnPantalla("Err img2"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

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

// ---- Actualizar huella (resumido) ----
void actualizarHuellaRemoto(int idHuella, int clienteId) {
  (void)clienteId;
  if (!sensorReady) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  mensajeEnPantalla("Actualizar (" + String(idHuella) + ")");
  while (finger.getImage() != FINGERPRINT_OK) { delay(80); }
  if (finger.image2Tz(1) != FINGERPRINT_OK) { mensajeEnPantalla("Err img"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }
  while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(60); }
  while (finger.getImage() != FINGERPRINT_OK) { delay(80); }
  if (finger.image2Tz(2) != FINGERPRINT_OK) { mensajeEnPantalla("Err img2"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }
  if (finger.createModel() != FINGERPRINT_OK) { mensajeEnPantalla("Err model"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }

  if (finger.storeModel(idHuella) == FINGERPRINT_OK) {
    mensajeEnPantalla("Huella actualizada");
    indicarExito();
  } else {
    mensajeEnPantalla("Err guardar");
    indicarFallo();
  }
  waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Borrar todas ----
void borrarTodasLasHuellas() {
  if (!sensorReady) { mensajeEnPantalla("Sensor OFF"); indicarFallo(); waitMsWithLed(1000); enterKeypadMode(); return; }
  if (finger.emptyDatabase() == FINGERPRINT_OK) {
    mensajeEnPantalla("BD borrada");
    indicarExito();
  } else {
    mensajeEnPantalla("Err borrar BD");
    indicarFallo();
  }
  waitMsWithLed(1000);
  enterKeypadMode();
}

// ---- Sincronizar (placeholder) ----
void sincronizarHuellasDesdeBackend() {
  mensajeEnPantalla("Sync (placeholder)");
  Serial.println("Sincronizar: aquí va tu lógica de descarga Base64 y storeModel().");
  indicarProcesando(); waitMsWithLed(1000);
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
