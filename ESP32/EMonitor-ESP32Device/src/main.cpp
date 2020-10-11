#include <Arduino.h>
#include <WiFi.h>
#include "xml.h"
#include "http.h"
#include "readings.h"
#include "wifi_con.h"

String serverBase = "http://c08c97d03562.ngrok.io";

void setup()
{
  Serial.begin(115200);
  delay(4000);
  wait_and_connect_to_wifi();
}

int msg = 0;
void loop()
{
  double temperature = round(readTemperature() * 100) / 100.0;
  double humidity = round(readHumidity() * 100) / 100.0;
  double pressure = round(readPressure() * 100) / 100.0;
  double light = round(readLightIntensity() * 100) / 100.0;

  char xmlchar[1500];
  char identifier[] = "MSG00001";
  char datetime[] = "2020-10-10T10:23:00+05:30";

  // Get the xml as a string to xmlchar variable
  generateXMLStr(xmlchar, temperature, humidity, pressure, light, identifier, datetime);

  if (WiFi.status() != WL_CONNECTED)
  {
    bool connected = connect_to_wifi();
    if (connected)
    {
      if (!sendPostRequest(xmlchar, msg))
      {
        Serial.printf("MSG %i Queued !\n\n", msg);
      }
    }
    else
    {
      Serial.printf("MSG %i Queued !\n\n", msg);
    }
  }
  else
  {
    if (!sendPostRequest(xmlchar, msg))
    {
      Serial.printf("MSG %i Queued !\n\n", msg);
    }
  }
  msg++;
  delay(10000);
}