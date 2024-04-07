#include <ESP8266WiFi.h>
#include "Util.h"

Util::Util()
{

}

void Util::printLocalTime()
{
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,80," %d %B %Y %H:%M:%S ",timeinfo);
  Serial.print("util: local time: ");
  Serial.println(buffer);
}

void Util::configTimeWithTZ(String tz)
{
  deviceTZ = tz;

  Serial.printf("util: config timezone %s", tz);
  configTime(deviceTZ.c_str(), ntpServer1.c_str());

  time_t rawtime;
  time(&rawtime);
  while (rawtime < 1000) {
    Serial.print(".");
    delay(500);
    time(&rawtime);
  }

  Serial.println("");
  Serial.printf("util: config timezone %s success\n", tz);
  printLocalTime();
}

void Util::updateTime()
{
  configTime(deviceTZ.c_str(), ntpServer1.c_str());
  Serial.println("util: update time. Time will be updated soon. It take a bit of seconds until server response");
}
