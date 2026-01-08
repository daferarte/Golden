// door_mod.h
#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>

// Inicializa el servo y sus parámetros mecánicos
void doorBegin(uint8_t servoPin, int minPulse, int maxPulse, int closedAngle, int openAngle);

// Abre y cierra la puerta con callbacks opcionales.
// - onOpenStart(): antes de abrir (útil para LCD/LED)
// - onCloseStart(): justo antes de cerrar (útil para LCD/LED)
// - onDone(): al terminar (útil para volver a modo teclado)
// - tick(): se llama periódicamente dentro de los waits (útil para updateLed())
bool doorOpenAndClose(unsigned long openMs,
                      unsigned long closeDelayMs,
                      void (*onOpenStart)() = nullptr,
                      void (*onCloseStart)() = nullptr,
                      void (*onDone)() = nullptr,
                      void (*tick)() = nullptr);

// (Opcional) saber si el servo está listo
bool doorIsAttached();
