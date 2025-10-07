// config.cpp
#include "config.h"

// ====== Pines ======
const int SERVO_PIN = 13;
const int SDA_PIN   = 21;
const int SCL_PIN   = 22;
const int FP_RX_PIN = 16;
const int FP_TX_PIN = 17;

const int PIN_R = 25;
const int PIN_G = 26;
const int PIN_B = 27;

// ====== WiFi ======
const char* ssid     = "daferDom";
const char* password = "d@f3R4Rt3$";

// ====== Servidor REST ======
const int   DEVICE_ID       = 1;
const char* SERVER_BASE_URL = "http://192.168.101.20:8000/api/v1/";

// ====== Timings ======
unsigned long POLLING_INTERVAL_MS   = 4000;
const unsigned long DOOR_OPEN_TIME_MS   = 5000;
const unsigned long DOOR_CLOSE_DELAY_MS = 3000;

// ====== Servo mecánica ======
const int SERVO_MIN_PULSE     = 544;
const int SERVO_MAX_PULSE     = 2400;
const int SERVO_CLOSED_ANGLE  = 0;
const int SERVO_OPEN_ANGLE    = 90;

// ====== Sensor huella ======
const int FINGERPRINT_SCANS    = 2;
const int MIN_MATCH_CONFIDENCE = 60;

// ====== MQTT ======
const char* MQTT_HOST = "192.168.101.20";
const int   MQTT_PORT = 1883;
const char* MQTT_USER = "backend";
const char* MQTT_PASS = "Deef3137047135$";

const char* SEDE = "pasto";
const char* DEV  = "turnstile-01";

// ====== Teclado Wiegand ======
const uint32_t CEDULA_TIMEOUT_MS = 15000;  // 15s
const char* SECRET_CODE = "2805";

// ====== LED pulso dorado ======
const unsigned long PULSE_PERIOD_MS = 2600;
const uint8_t PULSE_MIN  = 255;
const uint8_t PULSE_MAX  = 255;
const uint8_t GOLD_G_RATIO = 220;
const uint8_t GOLD_B_RATIO = 4;

// ====== LCD autodetect ======
uint8_t lcdAddr = 0x27;

// ====== Feedback LED fijo ======
const unsigned long FEEDBACK_MS = 2000;
