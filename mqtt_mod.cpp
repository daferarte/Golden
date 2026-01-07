// mqtt_mod.cpp
#include "mqtt_mod.h"
#include <ArduinoJson.h>
#include "fingerprint_mod.h"   // iniciarEscaneoHuella, actualizarHuellaRemoto, etc.

// ===== Objetos/constantes que vienen del .ino =====
extern PubSubClient mqtt;
extern const char* MQTT_HOST;
extern const int   MQTT_PORT;
extern const char* MQTT_USER;
extern const char* MQTT_PASS;

extern String T_CMD;
extern String T_ACK;
extern String T_STATE;
extern String T_EVENT;
extern String T_CONFIG;

// ===== Funciones del .ino / otros módulos que usamos =====
bool abrirPuerta(); // puerta/servo

// --- Huellas (fingerprint_mod) ---
void enrolarNuevoUsuario();
void actualizarHuellaRemoto(int idHuella, int clienteId);
void borrarTodasLasHuellas();
void sincronizarHuellasDesdeBackend();
void iniciarEscaneoHuella(); // para verify/search

// ---------- Publicaciones ----------
void mqttPublishState(bool online) {
  StaticJsonDocument<160> d;
  d["online"] = online;
  d["ts"] = (long)(millis()/1000);

  char buf[160];
  serializeJson(d, buf, sizeof(buf));
  mqtt.publish(T_STATE.c_str(), buf, true);  // retain=true
}

void publishEvent(const char* type, const char* info) {
  StaticJsonDocument<192> ev;
  ev["type"] = type;
  ev["ts"] = (long)(millis()/1000);
  if (info) ev["info"] = info;

  char js[192];
  serializeJson(ev, js, sizeof(js));
  mqtt.publish(T_EVENT.c_str(), js);         // retain=false (default)
}

// ---------- Callback de mensajes ----------
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  // Log del crudo
  Serial.printf("📥 MQTT mensaje en [%s]: ", topic);
  Serial.write(payload, len);
  Serial.println();

  StaticJsonDocument<512> d;
  DeserializationError derr = deserializeJson(d, payload, len);
  if (derr) {
    Serial.printf("❌ JSON inválido en cmd: %s\n", derr.c_str());
    return;
  }

  // Soporta ambos nombres: "action" y "comando"
  const char* action = d["action"] | d["comando"] | "";
  const char* id     = d["id"] | "";

  // Parámetros opcionales (acepta payload anidado y plano)
  JsonVariant p = d["payload"];
  int clienteId = 0;
  int idHuella  = 0;
  if (!p.isNull()) {
    clienteId = p["cliente_id"] | 0;
    idHuella  = p["id_huella"]  | 0;
  }
  // fallback si vienen planos
  if (clienteId == 0) clienteId = d["cliente_id"] | 0;
  if (idHuella  == 0) idHuella  = d["id_huella"]  | 0;

  bool ok = false;
  const char* err = nullptr;

  if (strcmp(action, "open_door") == 0 || strcmp(action, "open") == 0) {
    ok = abrirPuerta();
    publishEvent("door_open", ok ? "ok" : "fail");
  }
  else if (strcmp(action, "set_led") == 0 || strcmp(action, "led") == 0) {
    // Lectura de color
    int r = d["red"]   | -1;
    int g = d["green"] | -1;
    int b = d["blue"]  | -1;
    
    if (r >= 0 && g >= 0 && b >= 0) {
      // Guardar y aplicar (persistencia)
      saveLedColor((uint8_t)r, (uint8_t)g, (uint8_t)b);
      
      Serial.printf("🎨 LED Action: %d, %d, %d\n", r, g, b);
      publishEvent("led_set", "ok");
      ok = true;
    } else {
      err = "missing red/green/blue";
      Serial.println("❌ set_led sin colores");
    }
  }
  else if (strcmp(action, "update") == 0) {
    if (clienteId > 0 && idHuella > 0) {
      Serial.printf("➡️ update: cliente_id=%d, id_huella=%d\n", clienteId, idHuella);
      publishEvent("finger_update_cmd", "received");
      actualizarHuellaRemoto(idHuella, clienteId); // maneja UI/errores internamente
      ok = true; // recibimos y ejecutamos el comando
    } else {
      err = "missing cliente_id or id_huella";
      Serial.println("❌ update sin cliente_id/id_huella");
      publishEvent("cmd_update_invalid", "missing_fields");
    }
  }
  else if (strcmp(action, "enroll") == 0) {
    Serial.println("➡️ enroll");
    publishEvent("finger_enroll_cmd", "received");
    enrolarNuevoUsuario();
    ok = true;
  }
  else if (strcmp(action, "delete") == 0) {
    Serial.println("➡️ delete (borrar todas)");
    publishEvent("finger_delete_cmd", "received");
    borrarTodasLasHuellas();
    ok = true;
  }
  else if (strcmp(action, "sync") == 0) {
    Serial.println("➡️ sync");
    publishEvent("finger_sync_cmd", "received");
    sincronizarHuellasDesdeBackend();
    ok = true;
  }
  else if (strcmp(action, "verify") == 0 || strcmp(action, "search") == 0) {
    Serial.println("➡️ verify/search -> iniciarEscaneoHuella");
    publishEvent("finger_verify_cmd", "received");
    iniciarEscaneoHuella();  // entra en flujo: espera dedo, busca, valida backend
    ok = true;
  }
  else {
    err = "unknown action";
    Serial.printf("⚠️ action desconocida: '%s'\n", action);
  }

  // ACK al topic /cmd/ack
  StaticJsonDocument<192> ack;
  ack["id"] = id;
  ack["ok"] = ok;
  ack["action"] = action;
  if (err) ack["error"] = err;
  ack["ts"] = (long)(millis()/1000);

  char out[192];
  serializeJson(ack, out, sizeof(out));
  mqtt.publish(T_ACK.c_str(), out);
}

// ---------- Conexión / reconexión ----------
void ensureMqttConnected() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);

  while (!mqtt.connected()) {
    String cid = String("esp32-") + String((uint32_t)ESP.getEfuseMac(), HEX);

    // LWT: estado offline (retain)
    StaticJsonDocument<64> will;
    will["online"] = false;
    char willMsg[64];
    serializeJson(will, willMsg, sizeof(willMsg));

    if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS,
                     T_STATE.c_str(), 1, true, willMsg)) {
      mqtt.subscribe(T_CMD.c_str(), 1);
      mqtt.subscribe(T_CONFIG.c_str(), 1);
      mqttPublishState(true);
      Serial.println("✅ MQTT conectado");
    } else {
      Serial.println("❌ MQTT connect fallo, reintentando...");
      delay(1500);
    }
  }
}
