#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include "X9C10X.h"
#include "config.h"

const char *CONFIG_AP_NAME = "DVMThermometerConfig";
const unsigned long CONFIG_PORTAL_TIMEOUT = 30UL * 1000UL;

// Global Variables
X9C10X pot(Config::DIGIPOT_RESISTANCE_OHMS);
unsigned int consecutiveFailures = 0;
uint8_t currentPosition = 0; // Current position of the potentiometer
int tempCache = 0;
String wifiSSID;
String wifiPassword;

// Captive portal configuration
DNSServer dnsServer;
WebServer configServer(80);
Preferences preferences;
unsigned long portalStartedAt = 0;
unsigned long portalShutdownAt = 0;
bool portalRunning = false;
bool portalHasClientAtTimeout = false;
bool portalShutdownPending = false;

// NTP Configuration
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", Config::UTC_OFFSET_SECONDS, 60000);

void startNetworkAndPortal();
void serviceConfigPortal();
void stopConfigPortal();
void startStationConnection();
void handleConfigPage();
void handleConfigSubmit();
int fetchTemperature();
void updateDigipot(int temperature);
bool isDeadHours();
void rollDigits();

void setup() {
  pinMode(Config::DIGIPOT_INC_PIN, OUTPUT);
  pinMode(Config::DIGIPOT_UD_PIN, OUTPUT);
  pinMode(Config::DIGIPOT_CS_PIN, OUTPUT);

  Serial.begin(115200);

  startNetworkAndPortal();

  // Initialize Pot & Set to 0
  pot.begin(Config::DIGIPOT_INC_PIN, Config::DIGIPOT_UD_PIN,
            Config::DIGIPOT_CS_PIN);

  //ensures it is at 0
  updateDigipot(117);
  updateDigipot(15);
  pot.setPosition(0);

  Serial.printf("Location: %s\n", Config::WEATHER_LOCATION);
}

void loop() {
  static unsigned long lastUpdate = 15*60*1000;
  static unsigned long lastRollTime = 0;
  static bool wasWiFiConnected = false;
  const unsigned long UPDATE_INTERVAL = 60 * 1000;
  const unsigned long ROLL_INTERVAL = 60 * 1000; // Roll every minute during dead hours

  serviceConfigPortal();

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  bool wifiJustConnected = wifiConnected && !wasWiFiConnected;
  wasWiFiConnected = wifiConnected;

  // Update temperature every minute
  if (wifiJustConnected || millis() - lastUpdate > UPDATE_INTERVAL) {
    Serial.println("Fetching Temp");
    lastUpdate = millis();
    int temperature = fetchTemperature();
    if (temperature != -1) {
      updateDigipot(temperature);
      Serial.printf("Temp: %d\n", temperature);
      consecutiveFailures = 0;
    } else {
      consecutiveFailures++;
      if (consecutiveFailures >= 50) {
        Serial.println("Too many failures. Setting temperature to 0.");
        updateDigipot(0);
      }
    }
  }
  // Perform digit rolling during dead hours
  if (millis() - lastRollTime > ROLL_INTERVAL && isDeadHours()) {
    rollDigits();
    lastRollTime = millis();
  }
}

String escapeHtml(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

String configPage() {
  return String(F(
    "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Nixie DVM Thermometer Config</title></head><body>"
    "<h1>Nixie DVM Thermometer Config</h1>"
    "<form method=\"post\" action=\"/save\">"
    "<label>SSID <input name=\"ssid\" type=\"text\" value=\"")) +
    escapeHtml(wifiSSID) +
    F("\"></label><br>"
      "<label>Password <input name=\"password\" type=\"password\" value=\"") +
    escapeHtml(wifiPassword) +
    F("\"></label><br><button type=\"submit\">Submit</button>"
      "</form></body></html>");
}

void handleConfigPage() {
  configServer.send(200, "text/html", configPage());
}

void handleConfigSubmit() {
  wifiSSID = configServer.arg("ssid");
  wifiPassword = configServer.arg("password");

  preferences.begin("wifi", false);
  preferences.putString("ssid", wifiSSID);
  preferences.putString("password", wifiPassword);
  preferences.end();

  // Complete the HTTP response before taking down the access point.
  configServer.send(200, "text/html", configPage());
  portalShutdownPending = true;
  portalShutdownAt = millis() + 250;

  WiFi.disconnect(false, false);
  startStationConnection();
  Serial.println("Wi-Fi configuration saved.");
}

void startStationConnection() {
  if (wifiSSID.isEmpty()) {
    Serial.println("No saved Wi-Fi SSID; station connection skipped.");
    return;
  }

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  Serial.println("Connecting to saved Wi-Fi network in the background...");
}

void startNetworkAndPortal() {
  preferences.begin("wifi", true);
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(CONFIG_AP_NAME)) {
    Serial.println("Failed to start configuration access point.");
  } else {
    dnsServer.start(53, "*", WiFi.softAPIP());
    configServer.on("/", HTTP_GET, handleConfigPage);
    configServer.on("/save", HTTP_POST, handleConfigSubmit);
    configServer.onNotFound(handleConfigPage);
    configServer.begin();
    portalStartedAt = millis();
    portalRunning = true;
    Serial.printf("Configuration portal started at http://%s\n",
                  WiFi.softAPIP().toString().c_str());
  }

  startStationConnection();
}

void stopConfigPortal() {
  if (!portalRunning) return;

  configServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalRunning = false;
  portalShutdownPending = false;
  Serial.println("Configuration portal closed.");
}

void serviceConfigPortal() {
  if (!portalRunning) return;

  dnsServer.processNextRequest();
  configServer.handleClient();

  unsigned long now = millis();
  if (portalShutdownPending && (long)(now - portalShutdownAt) >= 0) {
    stopConfigPortal();
    return;
  }

  if (!portalHasClientAtTimeout && now - portalStartedAt >= CONFIG_PORTAL_TIMEOUT) {
    if (WiFi.softAPgetStationNum() == 0) {
      stopConfigPortal();
    } else {
      portalHasClientAtTimeout = true;
      Serial.println("Configuration client detected; portal will remain open until submit.");
    }
  }
}

int fetchTemperature() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String("http://api.openweathermap.org/data/2.5/weather?q=") +
                 Config::WEATHER_LOCATION + "&appid=" +
                 Config::OPENWEATHER_API_KEY + "&units=imperial";
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      int tempIndex = payload.indexOf("\"temp\":") + 7;
      int tempEnd = payload.indexOf(',', tempIndex);
      tempCache = payload.substring(tempIndex, tempEnd).toInt();
      http.end();
      return tempCache;
    } else {
      Serial.println("Failed to fetch temperature.");
      http.end();
      return -1;
    }
  } else {
    Serial.println("Wi-Fi not connected.");
    return -1;
  }
}

void updateDigipot(int temperature) {
  // Calculate target position based on temperature
  int targetPosition = temperature;
  // Calibrate these offsets if your DMM does not display the requested temperature.
  if(targetPosition < 39) targetPosition-=15;
  else if (targetPosition >= 39 && targetPosition < 77) targetPosition-=16;
  else if (targetPosition >= 77 && targetPosition < 107) targetPosition-=17;
  else targetPosition-=18;
  // Ensure targetPosition is within valid bounds
  if (targetPosition < 0) {
    targetPosition = 0;
  } else if (targetPosition > 99) {
    targetPosition = 99;
  }
  if (targetPosition == currentPosition) return;
  else {
    Serial.print("Current position: ");
    Serial.println(currentPosition);
    Serial.print("Target position: ");
    Serial.println(targetPosition);
    if (targetPosition > currentPosition){
      for (uint8_t i = currentPosition; i < targetPosition; i++){
        pot.incr(); // Increment position
        delay(10);  // Small delay for stabilization
      }
    }
    else if (targetPosition < currentPosition){
      for (uint8_t i = currentPosition; i > targetPosition; i--){
        pot.decr(); // Decrement position
        delay(10);  // Small delay for stabilization
      }
    }
    // Update currentPosition after adjustments
    currentPosition = targetPosition;
    pot.setPosition(currentPosition);
  }
}

bool isDeadHours() {
  static bool initialized = false;
  static unsigned long lastNTPAttempt = 0;
  const unsigned long NTP_RETRY_INTERVAL = 10000; // Retry every 10 seconds if NTP fails

  // Initialize NTP client once
  if (!initialized) {
    timeClient.begin();
    initialized = true;
  }

  // Update NTP time
  if (!timeClient.update()) {
    // Retry if enough time has passed since the last attempt
    if (millis() - lastNTPAttempt > NTP_RETRY_INTERVAL) {
      timeClient.forceUpdate(); // Force update attempt
      lastNTPAttempt = millis();
    }
    // If unable to get time, assume it's not dead hours to avoid disruptions
    return false;
  }

  int currentHour = timeClient.getHours();
  return currentHour >= Config::DEAD_HOURS_START &&
         currentHour < Config::DEAD_HOURS_END;
}

void rollDigits() {
  Serial.println("Rolling digits to prevent burnout...");
  updateDigipot(15);
  delay(100); 
  updateDigipot(22);
  delay(100); 
  updateDigipot(33);
  delay(100); 
  updateDigipot(44);
  delay(100); 
  updateDigipot(51);
  delay(100); 
  updateDigipot(66);
  delay(100); 
  updateDigipot(77);
  delay(100); 
  updateDigipot(88);
  delay(100); 
  updateDigipot(99);
  delay(100); 
  updateDigipot(100);
  delay(100); 
  updateDigipot(tempCache); // restore back to the temperature before the rolling
}
