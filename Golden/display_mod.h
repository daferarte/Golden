#pragma once
#include <Arduino.h>

// Inicializa el LCD (autodetecta 0x27/0x3F). Si no hay LCD, usa Serial.
void displayBegin();

// Mensaje de una o dos líneas (auto-particiona si hay espacio).
void mensajeEnPantalla(const String& mensaje);

// Mostrar exactamente dos líneas (cada una máx. 16 chars).
void displayShowTwoLines(const String& l1, const String& l2);

// Saber si hay LCD físico presente.
bool displayIsPresent();
