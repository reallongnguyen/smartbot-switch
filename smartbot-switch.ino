#include <string.h>
#include <ESP8266_ISR_Servo.h>
#include <ESP8266_ISR_Servo.hpp>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include <CronAlarms.h>
#include "config.h"
#include "Util.h"

#define SERVO 12
#define MIN_MICROS 500  //544 
#define MAX_MICROS 2450

Util util;

// wifi config
char ssid[30] = WIFI_SSID;
char password[30] = WIFI_PASS;

// device config
char spaceId[30] = SPACE_ID;
const char *STATE_ON = "on";
const char *STATE_OFF = "off";

// MQTT server config
const char *mqttBroker = MQTT_HOST;  // EMQX broker endpoint
const char *mqttUsername = DEVICE_ID;  // MQTT username for authentication
const char *mqttPassword = MQTT_PASS;  // MQTT password for authentication
const int mqttPort = MQTT_PORT;  // MQTT port (TCP)
String cmdTopic;
const char *pingTopic = "system/ping/iot_device";
String statusTopic;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectToWiFi();
void connectToMQTTBroker();
void mqttCallback(char *topic, byte *payload, unsigned int length);

int servoIndex = -1;
int range = 45;

void publishUpdateStatus(const char *state) {
  JSONVar statusMsg;
  statusMsg["iotDeviceId"] = DEVICE_ID;
  statusMsg["iotDeviceType"] = DEVICE_TYPE;
  statusMsg["state"] = state;

  String payload = JSON.stringify(statusMsg);

  mqttClient.publish(statusTopic.c_str(), payload.c_str());
  Serial.printf("publish update status msg: %s\n", payload.c_str());
}

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

  if (strcmp(state, STATE_ON) == 0) {
    position = 90 - range;
  } else if (strcmp(state, STATE_OFF) == 0) {
    position = 90 + range;
  } else {
    return;
  }

  ISR_Servo.setPosition(servoIndex, position);
  delay(200);
  publishUpdateStatus(state);
  delay(200);
  ISR_Servo.setPosition(servoIndex, 90);
  delay(400);
}

void turnOnSwitch() {
  setSwitchState(STATE_ON);
}

void updateTime() {
  util.updateTime();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  initSwitch();

  // MQTT
  cmdTopic = String(spaceId) + "/command/" + String(DEVICE_ID);
  statusTopic = String(spaceId) + "/device_status";

  connectToWiFi();
  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  connectToMQTTBroker();

  // sync time
  util.configTimeWithTZ("JST-9");

  // schedule
  Cron.create("0 */1 3 * * *", turnOnSwitch, false);
  Cron.create("0 0 */1 * * *", updateTime, false);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if (!mqttClient.connected()) {
      connectToMQTTBroker();
  }

  mqttClient.loop();

  Cron.delay();
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
      String clientId = "switch-client-" + String(DEVICE_ID) + "-" + String(WiFi.macAddress());
      Serial.printf("Connecting to MQTT Broker as %s.....\n", clientId.c_str());
      if (mqttClient.connect(clientId.c_str(), mqttUsername, mqttPassword)) {
          Serial.println("Connected to MQTT broker");
          mqttClient.subscribe(cmdTopic.c_str());
          Serial.printf("Subscribe topic %s\n", cmdTopic.c_str());
          // Publish message upon successful connection
          mqttClient.publish(pingTopic, DEVICE_ID);
      } else {
          Serial.print("Failed to connect to MQTT broker, rc=");
          Serial.print(mqttClient.state());
          Serial.println(" try again in 5 seconds");
          delay(5000);
      }
  }
}

void ack(String _spaceId, String requesterId, String requestId, String requestType, String status, String message, String newState) {
  String ackTopic = String(_spaceId) + "/ack/" + requesterId;

  JSONVar ackMsg;
  ackMsg["requestId"] = String(requestId);
  ackMsg["requestType"] = String(requestType);
  ackMsg["iotDeviceId"] = DEVICE_ID;
  ackMsg["iotDeviceType"] = DEVICE_TYPE;
  ackMsg["status"] = status;
  ackMsg["message"] = String(message);
  ackMsg["newState"] = String(newState);

  String payload = JSON.stringify(ackMsg);

  mqttClient.publish(ackTopic.c_str(), payload.c_str());
  Serial.printf("publish ack msg: %s\n", payload.c_str());
}

void handleCommand(JSONVar msg) {
  if (
    JSON.typeof(msg) == "undefined"
    || !msg.hasOwnProperty("command")
    || (strcmp(msg["command"], STATE_ON) != 0 && strcmp(msg["command"], STATE_OFF) != 0)
  ) {
    return;
  }

  ack(spaceId, msg["requesterId"], msg["id"], "command", "done", "", msg["command"]);

  setSwitchState(msg["command"]);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Message:");

  char payloadStr[length + 1];

  for (unsigned int i = 0; i < length; i++) {
      payloadStr[i] = payload[i];
  }

  payloadStr[length] = '\0';

  Serial.println(payloadStr);
  Serial.println("-----------------------");

  JSONVar msg = JSON.parse(payloadStr);

  if (strcmp(topic, cmdTopic.c_str()) == 0) {
    handleCommand(msg);
  }
}
