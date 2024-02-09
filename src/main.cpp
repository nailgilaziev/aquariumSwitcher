#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266HTTPClient.h>

#define WIFI_NAME "wi3"
#define WIFI_PASSWORD "qwER1234qwER1234"
#define WIFI_CONNECTION_TIMEOUT_MS 10000
#define WIFI_STATUS_LED_PIN LED_BUILTIN

#define DAYMS 86400000
#define TIMEZONE_MSK_OFFSET_SECONDS 10800

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ru.pool.ntp.org", TIMEZONE_MSK_OFFSET_SECONDS, DAYMS);

void blink(int count, int time = 50)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(WIFI_STATUS_LED_PIN, LOW);
    delay(time);
    digitalWrite(WIFI_STATUS_LED_PIN, HIGH);
    if (i != count - 1)
    {
      delay(time * 2);
    }
  }
}

/// правила для включения и выключения в течении суток
typedef struct
{
  int pin;
  String onRules;
  String offRules;
} portData;

#define PORTS_COUNT 4
portData ports[PORTS_COUNT];

void setup()
{
  Serial.begin(115200);
  // Serial.println("portTICK_PERIOD_MS = [" + String(portTICK_PERIOD_MS) + "]");

  pinMode(LED_BUILTIN, OUTPUT);

  ports[0] = {D0, "", ""};
  ports[1] = {D1, "", ""};
  ports[2] = {D2, "", ""};
  ports[3] = {D6, "", ""};

  for (int i = 0; i < PORTS_COUNT; i++)
  {
    portData port = ports[i];
    pinMode(port.pin, OUTPUT);
    // в high выключено
    digitalWrite(port.pin, HIGH);
  }

  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED)
  {
    blink(3, 20);
    Serial.print(".");
    delay(1000);
  }
  Serial.println();
  Serial.println("Connected!");
  blink(1, 2000);
  timeClient.begin();

  WiFiClientSecure wifiClient;
  wifiClient.setInsecure();
  HTTPClient http;
  http.begin(wifiClient, "https://raw.githubusercontent.com/arduino-libraries/NTPClient/master/examples/Advanced/Advanced.ino");

  int portsInitializationStatusCode = 0;
  while (portsInitializationStatusCode != 200)
  {
    portsInitializationStatusCode = http.GET();
    if (portsInitializationStatusCode == 200)
    {
      String payload = http.getString();
      Serial.println(payload);
      bool parsed = parseRules(payload);
      if (!parsed)
        for (;;)
        {
          Serial.print("Parsing error");
          blink(6);
          delay(10000);
        }
    }
    else
    {
      Serial.print("Error status code: ");
      Serial.println(portsInitializationStatusCode);
      blink(4);
      delay(10000);
    }
  }

  http.end();
}

// template 
// port/ruleType-rule1,rule2
// 1/ON-12:15,12:17
// 1/OFF-12:15,12:17
// 2/ON-12:15
// можно строчки комментить используя // 
bool parseRules(String resp)
{
  char charBuf[400];
  resp.toCharArray(charBuf, 400);
  char *cur = strtok(charBuf, "\n");
  while (cur != NULL)
  {
    String line = String(cur);
    Serial.println("parse line: " + line);
    if(line.length() < 10) return false;
    if(line.indexOf("/") < 0) return false;
    if(line.indexOf("-") < 0) return false;
    if(line.indexOf(":") < 0) return false;
    if(line.startsWith("//")) continue;
    int portIndex = line.substring(0, 1).toInt();
    Serial.println("portIndex: " + portIndex);
    String rules = line.substring(line.indexOf("-") + 1);
    String ruleType = line.substring(2, 4);
    if (ruleType == "ON")
    {
      ports[portIndex].onRules = rules;
      Serial.println("on rules: " + rules);
    }
    else
    {
      ports[portIndex].offRules = rules;
      Serial.println("off rules: " + rules);
    }
  }
}

void loop()
{
  blink(1);
  timeClient.update();
  if (!timeClient.isTimeSet())
  {
    Serial.println("Time sync error");
    blink(5);
    delay(1000);
    return;
  }
  String hms = timeClient.getFormattedTime();
  Serial.println(hms);
  String hm = hms.substring(0, 5);
  Serial.println(hm);
  Serial.println("Check h:m " + hm);
  for (int i = 0; i < PORTS_COUNT; i++)
  {
    portData port = ports[i];
    if (port.onRules.indexOf(hm) >= 0)
    {
      Serial.println("port[" + String(i) + "] ON. Rules: " + port.onRules);
      digitalWrite(port.pin, LOW);
    }
    if (port.offRules.indexOf(hm) >= 0)
    {
      Serial.println("port[" + String(i) + "] OFF. Rules: " + port.offRules);
      digitalWrite(port.pin, HIGH);
    }
  }
  delay(10000);
}