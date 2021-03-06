#include <Arduino.h>
#include <WiFi.h>
#include <queue>
using namespace std;
#include "xml.h"
#include "http.h"
#include "time.h"

#include "readings.h"
#include "wifi_con.h"
#include "config.h"
#include "utils.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "mail_client.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void beginOLED()
{
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  delay(2000);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 25);
  display.println("EMonitor");
  display.setTextSize(1);
  display.println("");
  display.println("GITHUB/ANJUCHAMANTHA");
  display.display();
  delay(5000);
  display.setCursor(0, 0);
}

String t0 = "";
String t1 = "";
String t2 = "";
String t3 = "";
String t4 = "";
String t5 = "";
String t6 = "";
String t7 = "";

void displayText()
{
  display.setCursor(0, 0);
  display.clearDisplay();
  display.println(t0);
  display.println(t1);
  display.println(t2);
  display.println(t3);
  display.println(t4);
  display.println(t5);
  display.println(t6);
  display.println(t7);
  display.display();
}

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

queue<String> buffer_identifier;
queue<String> buffer_datetime;
queue<int> buffer_msg_ids;

//mean buffers
queue<double> buffer_t;
queue<double> buffer_h;
queue<double> buffer_p;
queue<double> buffer_l;

//sd buffers
queue<double> buffer_t_;
queue<double> buffer_h_;
queue<double> buffer_p_;
queue<double> buffer_l_;

int msg = 0;
double temperature = 0;
double humidity = 0;
double pressure = 0;
double light = 0;
double temperature_sd = 0;
double humidity_sd = 0;
double pressure_sd = 0;
double light_sd = 0;

char xmlchar[1700];
String identifier;
char datetime_[32] = {};
String datetime;

void getTimeStamp(char *datetime_)
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
  }
  Serial.print("[TIME] ");
  // sprintf(times, &timeinfo, "%Y-%m-%dT%H:%M:%S%z");
  // Serial.println(&timeinfo, "%Y-%m-%dT%H:%M:%S%z");

  char timeString[32];
  time_t timeT = time(NULL);
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S%z", localtime(&timeT));
  printf("%s\n", timeString);
  sprintf(datetime_, timeString);
  // datetime_ = timeString;
}

void popBuffers()
{
  buffer_msg_ids.pop();
  buffer_identifier.pop();
  buffer_datetime.pop();

  buffer_t.pop();
  buffer_h.pop();
  buffer_p.pop();
  buffer_l.pop();

  buffer_t_.pop();
  buffer_h_.pop();
  buffer_p_.pop();
  buffer_l_.pop();
}

void pushToBuffers()
{
  buffer_msg_ids.push(msg);
  buffer_identifier.push(identifier);
  buffer_datetime.push(datetime);

  buffer_t.push(temperature);
  buffer_h.push(humidity);
  buffer_p.push(pressure);
  buffer_l.push(light);

  buffer_t_.push(temperature_sd);
  buffer_h_.push(humidity_sd);
  buffer_p_.push(pressure_sd);
  buffer_l_.push(light_sd);
}

void setup()
{
  Serial.begin(115200);
  beginOLED();
  t7 = String("WIFI   : 0");
  t6 = String("SERVER : 0");
  displayText();
  wait_and_connect_to_wifi();
  t7 = String("WIFI   : 1");
  displayText();
  begin_sensors();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

bool mailSent = false;
int mailTime = 10;

void loop()
{
  //BUFFER LOGIC

  //if buffer is not empty
  //    loop buffer
  //      if connected
  //        POST buffer data
  //      else break
  while (!buffer_msg_ids.empty())
  {
    Serial.println("[BUFFER] Processing Buffer... ");
    if (WiFi.status() == WL_CONNECTED)
    {
      t7 = String("WIFI   : 1");
      displayText();
      char xmlchar_buf[1700];

      int msg_id_buf = buffer_msg_ids.front();
      String identifier_buf = (String)buffer_identifier.front();
      String datetime_buf = (String)buffer_datetime.front();

      double t_buf = buffer_t.front();
      double h_buf = buffer_h.front();
      double p_buf = buffer_p.front();
      double l_buf = buffer_l.front();

      double t_sd_buf = buffer_t_.front();
      double h_sd_buf = buffer_h_.front();
      double p_sd_buf = buffer_p_.front();
      double l_sd_buf = buffer_l_.front();

      generateXMLStr(xmlchar_buf,
                     t_buf, h_buf, p_buf, l_buf,
                     t_sd_buf, h_sd_buf, p_sd_buf, l_sd_buf,
                     identifier_buf, datetime_buf);
      if (sendPostRequest(xmlchar_buf, msg_id_buf))
      {

        Serial.printf("[BUFFER] POST successful for : MSG %i\n\n", msg_id_buf);
        popBuffers();
        t6 = String("SERVER : 1 M-" + String(msg_id_buf) + " <B>");
        displayText();
      }
      else
      {
        t6 = String("SERVER : 0");
        displayText();
        Serial.printf("[BUFFER] POST Failed for : MSG %i\n\n", msg_id_buf);
        break;
      }
    }
    else
    {
      Serial.println("[WIFi] Not Connected !");
      t7 = String("WIFI   : 0");
      t6 = String("SERVER : 0");
      displayText();
      Serial.println("[BUFFER] Buffer Processing Skipped !\n\n");
      break;
    }
  }

  // MAIN LOGIC

  int x = 0;
  //Get 10 sensor values in 0.5s intervals
  //Calculate median & s.d for values in 10s intervals (10 readings x 1s)
  int rounds = 15;
  int round_time = 2000;

  //get values and keep in the arrays
  double t_[rounds];
  double h_[rounds];
  double p_[rounds];
  double l_[rounds];

  Serial.print("[SENSORS] Reading sensors > ");
  String temp = String("M-" + String(msg) + " ");
  while (x < rounds)
  {
    t_[x] = round(readTemperature() * 100) / 100.00;
    h_[x] = round(readHumidity() * 100) / 100.00;
    p_[x] = round(readPressure() * 100) / 100.00;
    l_[x] = round(readLightIntensity() * 100) / 100.00;
    Serial.print(".");
    temp = temp + "-";
    t5 = temp;
    displayText();
    delay(round_time);
    x++;
  }
  Serial.print("\n");

  temperature = calculate_mean(t_, rounds);
  humidity = calculate_mean(h_, rounds);
  pressure = calculate_mean(p_, rounds);
  light = calculate_mean(l_, rounds);

  temperature_sd = calculate_sd(t_, rounds, temperature);
  humidity_sd = calculate_sd(h_, rounds, humidity);
  pressure_sd = calculate_sd(p_, rounds, pressure);
  light_sd = calculate_sd(l_, rounds, light);

  Serial.printf("Temperature : %.2f +- %.2f %s \n", temperature, temperature_sd, "°C");
  Serial.printf("Humidity    : %.2f +- %.2f %s \n", humidity, humidity_sd, "%");
  Serial.printf("Pressure    : %.2f +- %.2f %s \n", pressure, pressure_sd, "Pa");
  Serial.printf("Light       : %.2f +- %.2f %s \n", light, light_sd, "%");
  Serial.print("\n");

  t0 = String("Temp     " + String(temperature) + "     C");
  t1 = String("Humidity " + String(humidity) + "     %");
  t2 = String("Pressure " + String(pressure) + " Pa");
  t3 = String("Light    " + String(light) + "     %");
  displayText();

  identifier = String(msg);
  getTimeStamp(datetime_);
  datetime = String(datetime_);

  if ((temperature > 40) && (WiFi.status() == WL_CONNECTED))
  {
    if (!mailSent || (mailTime < 1))
    {
      sendMail(temperature);
      mailSent = true;
      mailTime = 10;
    }
    mailTime--;
  }

  // Get the xml as a string to xmlchar variable
  generateXMLStr(xmlchar,
                 temperature, humidity, pressure, light,
                 temperature_sd, humidity_sd, pressure_sd, light_sd,
                 identifier, datetime);
  if (WiFi.status() != WL_CONNECTED)
  {
    t7 = String("WIFI   : 0");
    t6 = String("SERVER : 0");
    displayText();
    bool connected = connect_to_wifi();
    if (connected)
    {
      t7 = String("WIFI   : 1");
      displayText();
      if (!sendPostRequest(xmlchar, msg))
      {
        pushToBuffers();
        Serial.printf("[BUFFER] MSG %i Queued !\n\n", msg);
        t6 = String("SERVER : 0 M-" + String(msg) + " *");
        displayText();
      }
      t6 = String("SERVER : 1 M-" + String(msg));
      displayText();
    }
    else
    {
      pushToBuffers();
      Serial.printf("[BUFFER] MSG %i Queued !\n\n", msg);
      t7 = String("WIFI   : 0");
      t6 = String("SERVER : 0 M-" + String(msg) + " *");
      displayText();
    }
  }
  else
  {
    t7 = String("WIFI   : 1");
    displayText();
    if (!sendPostRequest(xmlchar, msg))
    {
      pushToBuffers();
      Serial.printf("[BUFFER] MSG %i Queued !\n\n", msg);
      t6 = String("SERVER : 0 M-" + String(msg) + " *");
      displayText();
    }
    else
    {
      t6 = String("SERVER : 1 M-" + String(msg));
      displayText();
    }
  }
  msg++;
}