// mqtt_mod.cpp
#include "mqtt_mod.h"
#include <ArduinoJson.h>

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
  mqtt.publish(T_EVENT.c_str(), js);
}

// ---------- Callback de mensajes ----------
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  // Log opcional
  Serial.printf("📥 MQTT mensaje en [%s]: ", topic);
  Serial.write(payload, len);
  Serial.println();

  StaticJsonDocument<512> d;
  if (deserializeJson(d, payload, len)) {
    Serial.println("❌ JSON inválido en cmd.");
    return;
  }

  // Soporta ambos nombres: "action" y "comando"
  const char* action = d["action"] | d["comando"] | "";
  const char* id     = d["id"] | "";

  // Parámetros opcionales
  int clienteId = d["cliente_id"] | 0;
  int idHuella  = d["id_huella"]  | 0;

  bool ok = false;
  const char* err = nullptr;

  if (strcmp(action, "open_door") == 0 || strcmp(action, "open") == 0) {
    ok = abrirPuerta();
    publishEvent("door_open", ok ? "ok" : "fail");
  }
  else if (strcmp(action, "update") == 0) {
    if (clienteId > 0 && idHuella > 0) {
      Serial.printf("➡️ update: cliente_id=%d, id_huella=%d\n", clienteId, idHuella);
      actualizarHuellaRemoto(idHuella, clienteId);
      ok = true; // la función maneja UI/errores; aquí confirmamos recibido
      publishEvent("finger_update_cmd", "received");
    } else {
      err = "missing cliente_id or id_huella";
      Serial.println("❌ update sin cliente_id/id_huella");
    }
  }
  else if (strcmp(action, "enroll") == 0) {
    Serial.println("➡️ enroll");
    enrolarNuevoUsuario();
    ok = true;
    publishEvent("finger_enroll_cmd", "received");
  }
  else if (strcmp(action, "delete") == 0) {
    Serial.println("➡️ delete (borrar todas)");
    borrarTodasLasHuellas();
    ok = true;
    publishEvent("finger_delete_cmd", "received");
  }
  else if (strcmp(action, "sync") == 0) {
    Serial.println("➡️ sync");
    sincronizarHuellasDesdeBackend();
    ok = true;
    publishEvent("finger_sync_cmd", "received");
  }
  else if (strcmp(action, "verify") == 0 || strcmp(action, "search") == 0) {
    Serial.println("➡️ verify/search -> iniciarEscaneoHuella");
    iniciarEscaneoHuella();
    ok = true;
    publishEvent("finger_verify_cmd", "received");
  }
  else {
    err = "unknown action";
    Serial.printf("⚠️ action desconocida: '%s'\n", action);
  }

  // ACK al topic /cmd/ack
  StaticJsonDocument<160> ack;
  ack["id"] = id;
  ack["ok"] = ok;
  ack["action"] = action;
  if (err) ack["error"] = err;
  ack["ts"] = (long)(millis()/1000);

  char out[160];
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
