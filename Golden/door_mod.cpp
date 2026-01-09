#include "door_mod.h"
#include "config.h"

// Variables para re-attach
extern const int SERVO_PIN; 
extern const int SERVO_MIN_PULSE;
extern const int SERVO_MAX_PULSE;

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

  // Re-attach por si estaba detached
  if (!door_servo.attached()) {
     door_servo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  }

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
  
  // Refuerzo: Enviar pulso repetido para asegurar cierre mecánico
  for(int i=0; i<8; i++) { // Aumentado a 8 para asegurar
     door_servo.write(g_closedAngle);
     delay(50);
  }

  // Importante: Esperar un poco más en posición cerrada antes de terminar
  t0 = millis();
  while (millis() - t0 < closeDelayMs) {
    if (tick) tick();
    smartDelay(10);
  }

  // Opcional: Detach para evitar vibración/consumo en reposo
  // door_servo.detach(); 

  // Fin
  if (onDone) onDone();
  return true;
}
