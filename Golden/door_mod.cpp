// door_mod.cpp
#include "door_mod.h"
#include "config.h"

static Servo door_servo;
static int   g_closedAngle = 0;
static int   g_openAngle   = 90;
static bool  g_attached    = false;

void doorBegin(uint8_t servoPin, int minPulse, int maxPulse, int closedAngle, int openAngle) {
  g_closedAngle = closedAngle;
  g_openAngle   = openAngle;

  door_servo.attach(servoPin, minPulse, maxPulse);
  door_servo.write(g_closedAngle);
  g_attached = true;
}

bool doorIsAttached() {
  return g_attached;
}

bool doorOpenAndClose(unsigned long openMs,
                      unsigned long closeDelayMs,
                      void (*onOpenStart)(),
                      void (*onCloseStart)(),
                      void (*onDone)(),
                      void (*tick)()) {
  if (!g_attached) return false;

  // Avisar que vamos a abrir (LCD/LEDs)
  if (onOpenStart) onOpenStart();

  // Abrir
  door_servo.write(g_openAngle);
  unsigned long t0 = millis();
  while (millis() - t0 < openMs) {
    if (tick) tick();
    smartDelay(10);
  }

  // Avisar que vamos a cerrar
  if (onCloseStart) onCloseStart();

  // Cerrar
  door_servo.write(g_closedAngle);
  t0 = millis();
  while (millis() - t0 < closeDelayMs) {
    if (tick) tick();
    smartDelay(10);
  }

  // Fin
  if (onDone) onDone();
  return true;
}
