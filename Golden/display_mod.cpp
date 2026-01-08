#include "display_mod.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"   // trae SDA_PIN y SCL_PIN

namespace {
  uint8_t g_addr = 0x27;
  LiquidCrystal_I2C* lcdp = nullptr;
  bool g_present = false;

  bool detectLCD() {
    // Asegura bus I2C con los pines de tu placa
    Wire.begin(SDA_PIN, SCL_PIN);
    for (uint8_t addr : { (uint8_t)0x27, (uint8_t)0x3F }) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) { g_addr = addr; return true; }
    }
    return false;
  }

  // recorta a 16 chars (si >16, toma los últimos 16 para que se vea el final)
  static String fit16Right(const String& s) {
    if (s.length() <= 16) return s;
    return s.substring(s.length() - 16);
  }
}

void displayBegin() {
  if (detectLCD()) {
    // crea/rehace el objeto
    if (lcdp) { delete lcdp; lcdp = nullptr; }
    lcdp = new LiquidCrystal_I2C(g_addr, 16, 2);
    lcdp->init();
    lcdp->backlight();
    g_present = true;
  } else {
    g_present = false;
  }
}

bool displayIsPresent() { return g_present; }

void displayShowTwoLines(const String& l1, const String& l2) {
  if (!g_present) {
    Serial.println("[LCD] " + l1 + (l2.length() ? (" | " + l2) : ""));
    return;
  }
  lcdp->clear();
  lcdp->setCursor(0, 0); lcdp->print(fit16Right(l1));
  lcdp->setCursor(0, 1); lcdp->print(fit16Right(l2));
}

void mensajeEnPantalla(const String& mensaje) {
  if (!g_present) {
    Serial.println("[LCD] " + mensaje);
    return;
  }
  // si cabe en una sola línea, la imprimimos tal cual en la línea 0
  if (mensaje.length() <= 16) {
    lcdp->clear();
    lcdp->setCursor(0, 0); lcdp->print(mensaje);
    return;
  }

  // si no, partimos "bonito" en un espacio antes del 16; si no hay, cortamos duro
  int pos = mensaje.lastIndexOf(' ', 16);
  if (pos == -1) pos = 16;
  String l1 = mensaje.substring(0, pos);
  String l2 = mensaje.substring(pos + 1);
  displayShowTwoLines(l1, l2);
}
