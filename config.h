// config.h
#pragma once
#include <Arduino.h>

// ====== Pines ======
extern const int SERVO_PIN;
extern const int SDA_PIN;
extern const int SCL_PIN;
extern const int FP_RX_PIN;
extern const int FP_TX_PIN;

extern const int PIN_R;
extern const int PIN_G;
extern const int PIN_B;

// ====== WiFi ======
extern const char* ssid;
extern const char* password;

// ====== Servidor REST ======
extern const int   DEVICE_ID;
extern const char* SERVER_BASE_URL;

// ====== Timings ======
extern unsigned long POLLING_INTERVAL_MS;
extern const unsigned long DOOR_OPEN_TIME_MS;
extern const unsigned long DOOR_CLOSE_DELAY_MS;

// ====== Servo mecánica ======
extern const int SERVO_MIN_PULSE;
extern const int SERVO_MAX_PULSE;
extern const int SERVO_CLOSED_ANGLE;
extern const int SERVO_OPEN_ANGLE;

// ====== Sensor huella ======
extern const int FINGERPRINT_SCANS;
extern const int MIN_MATCH_CONFIDENCE;

// ====== MQTT ======
extern const char* MQTT_HOST;
extern const int   MQTT_PORT;
extern const char* MQTT_USER;
extern const char* MQTT_PASS;

extern const char* SEDE;
extern const char* DEV;

// ====== Teclado Wiegand ======
extern const uint32_t CEDULA_TIMEOUT_MS;
extern const char* SECRET_CODE;

// ====== LED pulso dorado (usado por leds_mod) ======
extern const unsigned long PULSE_PERIOD_MS;
extern const uint8_t PULSE_MIN;
extern const uint8_t PULSE_MAX;
extern const uint8_t GOLD_G_RATIO;
extern const uint8_t GOLD_B_RATIO;

// ====== LCD autodetect ======
extern uint8_t lcdAddr;

// ====== Feedback LED fijo ======
extern const unsigned long FEEDBACK_MS;
