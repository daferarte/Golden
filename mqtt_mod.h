// mqtt_mod.h
#pragma once
#include <Arduino.h>
#include <PubSubClient.h>

// ====== Variables/objetos que YA declaras en tu .ino ======
extern PubSubClient mqtt;   // creado en tu .ino con WiFiClient
extern const char* MQTT_HOST;
extern const int   MQTT_PORT;
extern const char* MQTT_USER;
extern const char* MQTT_PASS;

extern String T_CMD;
extern String T_ACK;
extern String T_STATE;
extern String T_EVENT;
extern String T_CONFIG;

// ====== API pública (mismas funciones que ya usas) ======
void ensureMqttConnected();
void onMqttMessage(char* topic, byte* payload, unsigned int len);
void mqttPublishState(bool online);
void publishEvent(const char* type, const char* info = nullptr);
