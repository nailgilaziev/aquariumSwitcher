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

void blink(int count, int onTime = 100, int offTime = 300)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(WIFI_STATUS_LED_PIN, LOW);
    delay(onTime);
    digitalWrite(WIFI_STATUS_LED_PIN, HIGH);
    if (i != count - 1)
    {
      delay(offTime);
    }
  }
}

/// правила для включения и выключения в течении суток
typedef struct
{
  int pin;
  String onIntervals[10];
  int onIntervalsMins[10][2];
} portData;

#define PORTS_COUNT 4
portData ports[PORTS_COUNT];

void parsePortIntervals(String yaml)
{
  String remainingText = yaml;
  int lineEndIndex = remainingText.indexOf("\n");
  int portIndex = -1;
  int portIntervalIndex = -1;
  while (lineEndIndex != -1)
  {
    String line = remainingText.substring(0, lineEndIndex);
    remainingText = remainingText.substring(lineEndIndex + 1);
    lineEndIndex = remainingText.indexOf("\n");

    Serial.print(line);
    for (int i = line.length(); i < 20; i++)
    {
      Serial.print(" ");
    }
    
    if (line.startsWith("#"))
    {
      Serial.println("SKIP: is comment line");
      continue;
    }
    if (line.startsWith("port") && line.length() >= 6)
    {
      portIndex = line.substring(4, 5).toInt();
      portIntervalIndex = 0;
      Serial.println("PARSED: portIndex=" + String(portIndex));
      continue;
    }
    if (portIndex == -1)
    {
      Serial.println("SKIP: port index should be set first");
      continue;
    }
    if (!line.startsWith("  - '"))
    {
      Serial.println("SKIP: startWith incorrect");
      continue;
    }
    if (line.length() != 17)
    {
      Serial.println("SKIP: length incorrect");
      continue;
    }
    String interval = line.substring(5);
    if (portIntervalIndex >= 10)
    {
      Serial.println("SKIP: only 10 intervals allowed. current=" + String(portIntervalIndex));
      portIntervalIndex++;
      continue;
    }
    // portData port = ports[portIndex];
    Serial.print("[before=");
    Serial.print(ports[portIndex].onIntervals[portIntervalIndex]);
    Serial.print("] ");
    ports[portIndex].onIntervals[portIntervalIndex] = interval;
    Serial.print("[after=");
    Serial.print(ports[portIndex].onIntervals[portIntervalIndex]);
    Serial.print("] ");
  
    int ah = interval.substring(0, 2).toInt();
    int am = interval.substring(3, 5).toInt();
    int bh = interval.substring(6, 8).toInt();
    int bm = interval.substring(9).toInt();
    Serial.printf("PARSED: interval in mins %d*60 + %d - %d*60 + %d\n", ah, am, bh, bm);
    ports[portIndex].onIntervalsMins[portIntervalIndex][0] = ah * 60 + am;
    ports[portIndex].onIntervalsMins[portIntervalIndex][1] = bh * 60 + bm;
    portIntervalIndex++;
  }
}

String fetchPortIntervalsData()
{
  WiFiClientSecure wifiClient;
  wifiClient.setInsecure();
  HTTPClient http;
  http.begin(wifiClient, "https://nailgilaziev.github.io/aquariumSwitcher/ports.yaml");

  int portsInitializationStatusCode = http.GET();
  if (portsInitializationStatusCode != 200)
  {
    Serial.print("Error status code: ");
    Serial.println(portsInitializationStatusCode);
    return "";
  }

  String payload = http.getString();
  //Serial.println(payload);
  http.end();
  wifiClient.stop();
  return payload;
}

void printPorts()
{
  Serial.println();
  Serial.println("Ports summary:");
  for (int i = 0; i < PORTS_COUNT; i++)
  {
    portData port = ports[i];
    Serial.print("PORT " + String(i) + "   ");
    for (int i = 0; i < 10; i++)
    {
      String onInterval = port.onIntervals[i];
      if(onInterval==NULL) continue;
      Serial.print(onInterval);
      Serial.print("(");
      Serial.print(String(port.onIntervalsMins[i][0]) + "-" + String(port.onIntervalsMins[i][1]));
      Serial.print(") ");
    }
    Serial.println();
  }
}

void setup()
{
  Serial.begin(115200);

  ports[0] = {D0};
  ports[1] = {D1};
  ports[2] = {D2};
  ports[3] = {D6};
  for (int i = 0; i < PORTS_COUNT; i++)
  {
    portData port = ports[i];
    pinMode(port.pin, OUTPUT);
    // в high выключено
    digitalWrite(port.pin, HIGH);
  }
  pinMode(LED_BUILTIN, OUTPUT);

  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  Serial.print("Connecting...");
  /// надо обязательно подключиться, чтобы получить текущее время.
  while (WiFi.status() != WL_CONNECTED)
  {
    blink(1, 250);
    Serial.print(".");
    delay(250);
  }
  Serial.println();
  Serial.println("Connected!");
  blink(1, 2000);

  timeClient.begin();
  timeClient.update();
  while (!timeClient.isTimeSet())
  {
    Serial.println("Time sync error");
    blink(3);
    timeClient.update();
    delay(5000);
  }
  Serial.println("Time synced!");
  String yaml = fetchPortIntervalsData();
  while (yaml.isEmpty())
  {
    Serial.println("Fetch intervals error");
    blink(4);
    yaml = fetchPortIntervalsData();
    delay(5000);
  }
  Serial.println("Interval yaml fetched!");
  parsePortIntervals(yaml);
  Serial.println("Interval yaml parsed!");
  printPorts();
  Serial.println("------------------");
}

void actualizePortsForTime(String hm)
{
  Serial.println("ActualizePortsForTime hh:mm = " + hm);
  int h = hm.substring(0, 2).toInt();
  int m = hm.substring(3, 5).toInt();
  int curMins = h * 60 + m;
  for (int pi = 0; pi < PORTS_COUNT; pi++)
  {
    portData port = ports[pi];
    bool shouldBeOn = false;
    for (int i = 0; i < 10; i++)
    {
      String interval = port.onIntervals[i];
      if (interval == NULL)
        break;
      int a = port.onIntervalsMins[i][0];
      int b = port.onIntervalsMins[i][1];
      if (a <= curMins && curMins < b)
      {
        Serial.println("interval " + interval + " is active");
        shouldBeOn = true;
      }
    }
    if (shouldBeOn)
    {
      digitalWrite(port.pin, LOW);
    }
    else
    {
      digitalWrite(port.pin, HIGH);
    }
  }
}

void loop()
{
  timeClient.update();
  blink(1);
  String hms = timeClient.getFormattedTime();
  String hm = hms.substring(0, 5);
  actualizePortsForTime(hm);
  delay(10000);
}