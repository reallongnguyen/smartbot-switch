#ifndef Util_h
#define Util_h

#include <ESP8266WiFi.h>

class Util
{
  public:
    Util();
    void printLocalTime();
    void configTimeWithTZ(String tz);
    void updateTime();
  private:
    String deviceTZ = "UTC";
    String ntpServer1 = "pool.ntp.org";
};

#endif
