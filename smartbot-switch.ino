#include "string.h"
#include <ESP8266_ISR_Servo.h>
#include <ESP8266_ISR_Servo.hpp>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include "./config.h"

#define SERVO 12
#define MIN_MICROS 500  //544 
#define MAX_MICROS 2450

// wifi config
char ssid[30] = WIFI_SSID;
char password[30] = WIFI_PASS;

// device config
const char *deviceId = DEVICE_ID;
char spaceId[30] = SPACE_ID;
const char *STATE_ON = "on";
const char *STATE_OFF = "off";

// MQTT server config
const char *mqttBroker = MQTT_HOST;  // EMQX broker endpoint
const char *mqttUsername = deviceId;  // MQTT username for authentication
const char *mqttPassword = MQTT_PASS;  // MQTT password for authentication
const int mqttPort = MQTT_PORT;  // MQTT port (TCP)
char *cmdTopic;     // MQTT topic
const char *pingTopic = "ping/iot_devices";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectToWiFi();
void connectToMQTTBroker();
void mqttCallback(char *topic, byte *payload, unsigned int length);

int servoIndex = -1;
int range = 45;

void initSwitch() {
  servoIndex = ISR_Servo.setupServo(SERVO, MIN_MICROS, MAX_MICROS);
  ISR_Servo.setPosition(servoIndex, 90);
  delay(400);
}

void setSwitchState(const char *state) {
  if (servoIndex == -1) {
    return;
  }

  int position = 0;

  if (strcmp(state, "on") == 0) {
    position = 90 - range;
  } else if (strcmp(state, "off") == 0) {
    position = 90 + range;
  } else {
    return;
  }

  ISR_Servo.setPosition(servoIndex, position);
  delay(400);
  ISR_Servo.setPosition(servoIndex, 90);
  delay(400);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  initSwitch();

  // MQTT
  String cmdTopicS = String(spaceId) + "/commands/" + String(deviceId);
  cmdTopic = (char *)malloc(cmdTopicS.length() + 1);
  strcpy(cmdTopic, cmdTopicS.c_str());

  connectToWiFi();
  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  connectToMQTTBroker();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if (!mqttClient.connected()) {
      connectToMQTTBroker();
  }

  mqttClient.loop();
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("\nConnected to the WiFi network");
}

void connectToMQTTBroker() {
  while (!mqttClient.connected()) {
      String clientId = "switch-client-" + String(deviceId) + "-" + String(WiFi.macAddress());
      Serial.printf("Connecting to MQTT Broker as %s.....\n", clientId.c_str());
      if (mqttClient.connect(clientId.c_str(), mqttUsername, mqttPassword)) {
          Serial.println("Connected to MQTT broker");
          mqttClient.subscribe(cmdTopic);
          Serial.printf("Subscribe topic %s\n", cmdTopic);
          // Publish message upon successful connection
          mqttClient.publish(pingTopic, deviceId);
      } else {
          Serial.print("Failed to connect to MQTT broker, rc=");
          Serial.print(mqttClient.state());
          Serial.println(" try again in 5 seconds");
          delay(5000);
      }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Message:");

  char *payloadStr = (char*)malloc(length + 1);

  for (unsigned int i = 0; i < length; i++) {
      payloadStr[i] = payload[i];
  }

  payloadStr[length] = '\0';

  Serial.println(payloadStr);
  Serial.println("-----------------------");

  if (strcmp(topic, cmdTopic) != 0) {
    return;
  }

  JSONVar msg = JSON.parse(payloadStr);

  if (JSON.typeof(msg) == "undefined" || !msg.hasOwnProperty("command")) {
    return;
  }

  if (strcmp(msg["command"], STATE_ON) != 0 && strcmp(msg["command"], STATE_OFF) != 0) {
    return;
  }

  String ackTopic = String(spaceId) + "/ack_commands/" + String(msg["requesterId"]);
  Serial.printf("ackTopic %s\n", ackTopic.c_str());

  char commandId[strlen(msg["id"]) + 1];
  char newState[strlen(msg["command"]) + 1];

  strcpy(commandId, msg["id"]);
  strcpy(newState, msg["command"]);

  JSONVar ackMsg;
  ackMsg["commandId"] = commandId;
  ackMsg["iotDeviceId"] = deviceId;
  ackMsg["iotDeviceType"] = "switch";
  ackMsg["status"] = "done";
  ackMsg["newState"] = newState;

  String mqttMsg = JSON.stringify(ackMsg);

  mqttClient.publish(ackTopic.c_str(), mqttMsg.c_str());
  Serial.printf("publish ack msg: %s\n", mqttMsg.c_str());

  setSwitchState(newState);

  free(payloadStr);
}
