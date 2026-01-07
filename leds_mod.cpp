// leds_mod.cpp
#include "leds_mod.h"
#include "config.h"

// Helpers internos (no expuestos)
static uint8_t pulseBrightness() {
  unsigned long t = millis() % PULSE_PERIOD_MS;
  unsigned long half = PULSE_PERIOD_MS / 2;
  unsigned long span = (unsigned long)(PULSE_MAX - PULSE_MIN);
  unsigned long val = (t < half)
    ? (PULSE_MIN + (span * t) / half)
    : (PULSE_MIN + (span * (PULSE_PERIOD_MS - t)) / half);
  if (val > 255) val = 255;
  return (uint8_t)val;
}

static void drawPulseGold() {
  uint8_t base = pulseBrightness();
  uint8_t r = base;
  uint8_t g = (uint16_t)base * GOLD_G_RATIO / 255;
  uint8_t b = (uint16_t)base * GOLD_B_RATIO / 255;
  setColor(r, g, b);
}

// ===== Implementaciones movidas tal cual =====
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_R, r);
  analogWrite(PIN_G, g);
  analogWrite(PIN_B, b);
}

void triggerFeedback(FixedKind kind, unsigned long ms) {
  fixedKind = kind;
  ledMode = MODE_FIXED;
  feedbackUntil = millis() + ms;
  if (kind == FIX_GREEN) setColor(0,255,0);
  else                   setColor(255,0,0);
}

void updateLed() {
  if (ledMode == MODE_FIXED) {
    if ((long)(millis() - feedbackUntil) < 0) return;
    ledMode = MODE_STATIC; // Regresar a modo estático (antes era PULSE)
  }
  
  if (ledMode == MODE_STATIC) {
    setColor(currentR, currentG, currentB);
  } else {
    // Fallback: si por alguna razón sigue en pulse (no debería)
    drawPulseGold();
  }
}

void waitMsWithLed(unsigned long ms) {
  unsigned long t0 = millis();
  while ((millis() - t0) < ms) {
    updateLed();
    smartDelay(10); // Mantiene MQTT vivo mientras espera
  }
}
