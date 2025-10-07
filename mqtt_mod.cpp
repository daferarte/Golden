// mqtt_mod.cpp
#include "mqtt_mod.h"
#include <ArduinoJson.h>

// Objetos/constantes que vienen del .ino:
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

// Funciones del .ino que usamos:
bool abrirPuerta(); // para ejecutar comandos desde MQTT

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
  StaticJsonDocument<512> d;
  if (deserializeJson(d, payload, len)) return;

  const char* id = d["id"] | "";
  const char* action = d["action"] | "";
  bool ok = false;

  if (strcmp(action, "open_door") == 0 || strcmp(action, "open") == 0) {
    ok = abrirPuerta();
    publishEvent("door_open");
  }

  StaticJsonDocument<96> ack;
  ack["id"] = id;
  ack["ok"] = ok;

  char out[96];
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

    // LWT: estado offline retain
    StaticJsonDocument<64> will;
    will["online"] = false;
    char willMsg[64];
    serializeJson(will, willMsg, sizeof(willMsg));

    if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, T_STATE.c_str(), 1, true, willMsg)) {
      mqtt.subscribe(T_CMD.c_str(), 1);
      mqtt.subscribe(T_CONFIG.c_str(), 1);
      mqttPublishState(true);
    } else {
      delay(1500);
    }
  }
}
