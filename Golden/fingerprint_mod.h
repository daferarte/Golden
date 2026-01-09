// fingerprint_mod.h
#pragma once
#include <Arduino.h>

// ==== Constantes/Config que están en el .ino ====
extern const int FP_RX_PIN;
extern const int FP_TX_PIN;
extern const int MIN_MATCH_CONFIDENCE;
extern const char* SERVER_BASE_URL;

// ==== UI / Display (viene de display_mod) ====
void mensajeEnPantalla(const String& mensaje);  // implementada en display_mod

// ==== Wrappers de UI/LED que VIVEN en tu .ino (o leds_mod) ====
// (Las declaramos como extern para poder usarlas desde este módulo)
extern void indicarExito();
extern void indicarFallo();
extern void indicarProcesando();
extern void waitMsWithLed(unsigned long ms);

// Esta función vive en tu .ino:
extern void enterKeypadMode();

// MQTT helper que ya tienes implementado (mqtt_mod / .ino)
void publishEvent(const char* type, const char* info);

// Acción de puerta (door_mod / .ino)
bool abrirPuerta();

// ===== API PÚBLICA DEL MÓDULO DE HUELLA =====
extern bool sensorReady;
extern unsigned long nextSensorRetryAt;
extern const unsigned long SENSOR_RETRY_MS;
extern const unsigned long SENSOR_RETRY_MAX;
extern unsigned long sensorRetryDelay;

bool initFingerprintSensor(bool showLCD = true, uint8_t retries = 3);
void scheduleSensorRetry(unsigned long ms);

// Operaciones
void iniciarEscaneoHuella();
void mostrarEstadisticasSensor();
void enrolarNuevoUsuario();
void actualizarHuellaRemoto(int idHuella, int clienteId);
void verificarAcceso();
void identificarUsuario();
void borrarTodasLasHuellas();
void sincronizarHuellasDesdeBackend();
