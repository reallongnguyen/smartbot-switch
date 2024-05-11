#include <string.h>
#include <ESP8266_ISR_Servo.h>
#include <ESP8266_ISR_Servo.hpp>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include <CronAlarms.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "config.h"
#include "Util.h"

#define SERVO 12
#define MIN_MICROS 500  //544 
#define MAX_MICROS 2450

Util util;

// mode: setup home WiFi
const char* apSsid = AP_WIFI_SSID;
const char* apPassword = AP_WIFI_PASS;
const int port = 80;
WiFiServer server(port);
IPAddress local_IP(192, 168, 4, 1);

// mode: connect home WiFi config
char ssid[64] = WIFI_SSID;
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
String statusTopic;
String scheduleTopic;
const char *syncSettingTopic = "system/sync_setting/iot_device";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void connectToWiFi();
void connectToMQTTBroker();
void mqttCallback(char *topic, byte *payload, unsigned int length);

int servoIndex = -1;
int range = 45;

void publishUpdateStatus(const char *state, const char *trigger, char *triggerData);
void initSwitch();
void setSwitchState(const char *state, char *trigger, char *payload);

CronID_t cronIds[16];
int cronLen = 0;

void turnOnSwitch() {
  setSwitchState(STATE_ON, "scheduler", "");
}

void turnOffSwitch() {
  setSwitchState(STATE_OFF, "scheduler", "");
}

void updateTime() {
  util.updateTime();
}

int setupAfterConnectWiFiSuccess = 0;

bool connectToHomeWiFi(const char* ssid, const char* password, int attempt);
void setupHomeWiFi();
void setupAfterConnectWiFi();
bool needSetupHomeWiFi() {
  return strcmp(ssid, "") == 0;
}

void configWiFiAP() {
  Serial.println("Setting up access point...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(local_IP, local_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid, apPassword);
  Serial.print("Access point created with SSID: ");
  Serial.println(apSsid);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // device
  initSwitch();

  // setup WiFi AP
  configWiFiAP();

  server.begin();
  Serial.println("Server started");
}

void loop() {
  if (needSetupHomeWiFi()) {
    setupHomeWiFi();
    delay(1);

    return;
  }

  if (!setupAfterConnectWiFiSuccess) {
    server.stop();
    connectToHomeWiFi(ssid, password, true, 0);
    setupAfterConnectWiFi();
    delay(15);

    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    connectToHomeWiFi(ssid, password, true, 0);
  }

  if (!mqttClient.connected()) {
    connectToMQTTBroker();
  }

  mqttClient.loop();

  Cron.delay();
}

void connectToMQTTBroker() {
  while (!mqttClient.connected()) {
      Serial.printf("Connecting to MQTT Broker as %s.....\n", MQTT_CLIENT_ID);
      if (mqttClient.connect(MQTT_CLIENT_ID, mqttUsername, mqttPassword)) {
          Serial.println("Connected to MQTT broker");
          mqttClient.subscribe(cmdTopic.c_str());
          Serial.printf("Subscribe topic %s\n", cmdTopic.c_str());
          mqttClient.subscribe(scheduleTopic.c_str());
          Serial.printf("Subscribe topic %s\n", scheduleTopic.c_str());
      } else {
          Serial.print("Failed to connect to MQTT broker, rc=");
          Serial.print(mqttClient.state());
          Serial.println(" try again in 5 seconds");
          delay(5000);
      }
  }
}

void handleCommand(JSONVar msg) {
  if (
    JSON.typeof(msg) == "undefined"
    || !msg.hasOwnProperty("command")
    || (strcmp(msg["command"], STATE_ON) != 0 && strcmp(msg["command"], STATE_OFF) != 0)
  ) {
    return;
  }

  setSwitchState(msg["command"], "command", (char *)(const char*)msg["requesterId"]);
}

void handleSchedule(JSONVar msg) {
  Serial.println(msg);
  int lenSchedule = msg["schedules"].length();
  int i;

  for (i = 0; i < cronLen; i++) {
    Cron.free(cronIds[i]);
    cronIds[i] = dtINVALID_ALARM_ID;
  }

  cronLen = lenSchedule;

  for (i = 0; i < lenSchedule; i++) {
    char *schedule = (char*)(const char*)msg["schedules"][i]["schedule"];
    bool isRepeat = (bool)msg["schedules"][i]["isRepeat"];
    JSONVar action = msg["schedules"][i]["action"];

    if (strcmp(action, "on") == 0) {
      cronIds[i] = Cron.create(schedule, turnOnSwitch, !isRepeat);
    }

    if (strcmp(action, "off") == 0) {
      cronIds[i] =  Cron.create(schedule, turnOffSwitch, !isRepeat);
    }
  }
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
  } else if (strcmp(topic, scheduleTopic.c_str()) == 0) {
    handleSchedule(msg);
  }
}

bool connectToHomeWiFi(const char* ssid, const char* password, bool onlySTA, int attempt) {
  if (onlySTA) {
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to home WiFi: ");
  Serial.println(ssid);
  int connectionRetries = 0;
  while (WiFi.status() != WL_CONNECTED && (attempt == 0 || connectionRetries < attempt)) {
    delay(500);
    Serial.print(".");
    connectionRetries++;
  }

  return WiFi.status() == WL_CONNECTED;
}

void setupHomeWiFi() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("Client connected");
    while (client.connected()) {
      if (!client.available()) { continue; }

      String request = client.readStringUntil('{');
      Serial.print("request: "); Serial.println(request);

      if (request.indexOf("OPTIONS") != -1) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");  // Optional: Allow cross-origin requests
        client.println("Access-Control-Allow-Headers: authorization, x-client-info, apikey, content-type");
        client.println("Access-Control-Request-Method: *");
        client.println();
        client.println("ok");

        break;
      }

      if (request.indexOf("/ping") != -1 && request.indexOf("GET") != -1) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");  // Optional: Allow cross-origin requests
        client.println();
        client.println("OK");

        break;
      }

      if (request.indexOf("/config") == -1 || request.indexOf("POST") == -1) {
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");
        client.println();
        client.println("");

        break;
      }

      String payload = "{";
      while (client.available()) {
        payload += client.readStringUntil('&');
      }
      payload.trim();

      JSONVar body = JSON.parse(payload);

      Serial.print("Payload: "); Serial.println(payload);

      String homeSSID = body["ssid"];
      String homePassword = body["password"];
      String spaceIdStr = body["spaceId"];

      strcpy(spaceId, spaceIdStr.c_str());

      Serial.print("Received SSID: ");
      Serial.println(homeSSID);
      Serial.print("Received password: ");
      Serial.println(homePassword);

      // Connect to home WiFi
      if (connectToHomeWiFi(homeSSID.c_str(), homePassword.c_str(), false, 30)) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");  // Optional: Allow cross-origin requests
        client.println();
        client.println("");

        strcpy(ssid, homeSSID.c_str());
        strcpy(password, homePassword.c_str());

        Serial.println("Setup home WiFi successfully. Exit setup WiFi state");
      } else {
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println("Access-Control-Allow-Origin: *");
        client.println();
        client.println("");
      }

      break;
    }

    client.stop();
    Serial.println("Client stopped");
  }
}

void setupAfterConnectWiFi() {
    // sync time
    util.configTimeWithTZ("JST-9");

    // MQTT
    cmdTopic = String(spaceId) + "/command/" + String(DEVICE_ID);
    statusTopic = String(spaceId) + "/iot_device/update/" + String(DEVICE_ID);
    scheduleTopic = "system/schedule/" + String(DEVICE_ID);

    mqttClient.setServer(mqttBroker, mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);
    connectToMQTTBroker();

    mqttClient.publish(syncSettingTopic, DEVICE_ID);

    // schedule
    Cron.create(SCHEDULE_UPDATE_TIME, updateTime, false);

    setupAfterConnectWiFiSuccess = 1;
}

void publishUpdateStatus(const char *state, const char *trigger, char *triggerData) {
  JSONVar statusMsg;
  statusMsg["iotDeviceId"] = DEVICE_ID;
  statusMsg["iotDeviceType"] = DEVICE_TYPE;
  statusMsg["state"] = state;
  statusMsg["trigger"] = trigger;
  statusMsg["triggerData"] = triggerData;

  String payload = JSON.stringify(statusMsg);

  mqttClient.publish(statusTopic.c_str(), payload.c_str());
  Serial.printf("publish update status msg: %s\n", payload.c_str());
}

void initSwitch() {
  servoIndex = ISR_Servo.setupServo(SERVO, MIN_MICROS, MAX_MICROS);
  ISR_Servo.setPosition(servoIndex, 90);
  delay(400);
}

void setSwitchState(const char *state, char *trigger, char *payload) {
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
  publishUpdateStatus(state, trigger, payload);
  delay(200);
  ISR_Servo.setPosition(servoIndex, 90);
  delay(400);
}
