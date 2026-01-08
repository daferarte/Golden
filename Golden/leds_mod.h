// leds_mod.h
#pragma once
#include <Arduino.h>

// ===== Tipos y estado (ya existen en tu .ino) =====
// Deja estas ENUM y variables DEFINIDAS en tu .ino.
// Aquí solo las "vemos" para poder usarlas en el módulo.
enum LedMode   { MODE_PULSE, MODE_FIXED, MODE_STATIC };
enum FixedKind { FIX_GREEN, FIX_RED };

extern LedMode  ledMode;          // definido en tu .ino
extern FixedKind fixedKind;       // definido en tu .ino
extern unsigned long feedbackUntil;  // definido en tu .ino
extern uint8_t currentR, currentG, currentB; // Color estático actual

// ===== Constantes/pines que ya defines en tu .ino =====
extern const unsigned long PULSE_PERIOD_MS;
extern const uint8_t PULSE_MIN;
extern const uint8_t PULSE_MAX;
extern const uint8_t GOLD_G_RATIO;
extern const uint8_t GOLD_B_RATIO;

extern const unsigned long FEEDBACK_MS;

extern const int PIN_R;
extern const int PIN_G;
extern const int PIN_B;

// ===== API de LEDs (misma que ya usas) =====
void setColor(uint8_t r, uint8_t g, uint8_t b);
void triggerFeedback(FixedKind kind, unsigned long ms = FEEDBACK_MS);
void updateLed();
void waitMsWithLed(unsigned long ms);
