#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <DNSServer.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;

// ---------- Config ----------
#define EEPROM_SIZE 96
const char* mqtt_server = "server.iistbihar.com";
const int mqtt_port = 1883;
const char* mqtt_user = "device_1";
const char* mqtt_pass = "testpass";

String baseTopic = "smartHome/" + String(mqtt_user) + "/";

// ---------- Relay Pins ----------
int relayPins[8] = {5, 26, 18, 19, 21, 22, 23, 25};  // your relay pins
int switchPins[8] = {32, 33, 34, 35, 36, 39, 14, 27};  // your manual switches
int lastSwitchStates[8];

// ---------- Objects ----------
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- EEPROM ----------
void saveWiFiCredentials(String ssid, String pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) EEPROM.write(i, i < ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < 64; i++) EEPROM.write(32 + i, i < pass.length() ? pass[i] : 0);
  EEPROM.commit();
}

void loadWiFiCredentials(char* ssid, char* pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) ssid[i] = EEPROM.read(i);
  ssid[32] = '\0';
  for (int i = 0; i < 64; i++) pass[i] = EEPROM.read(32 + i);
  pass[64] = '\0';
}

bool connectToWiFiFromEEPROM() {
  char ssid[33] = {0};
  char pass[65] = {0};
  loadWiFiCredentials(ssid, pass);
  if (strlen(ssid) == 0) return false;

  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---------- Web Config ----------
void handleRoot() {
  String html = "<html><body><h2>ESP32 WiFi Config</h2>"
                "<form action='/save' method='post'>"
                "SSID: <input name='ssid'><br>"
                "Password: <input name='pass' type='password'><br>"
                "<input type='submit' value='Save & Reboot'>"
                "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    saveWiFiCredentials(server.arg("ssid"), server.arg("pass"));
    server.send(200, "text/html", "<h3>Saved. Rebooting...</h3>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing SSID or Password");
  }
}

void startAPMode() {
  String apSSID = "SMART_HOME_" + String(mqtt_user);
  WiFi.softAP(apSSID.c_str());

  IPAddress IP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", IP);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Wi-Fi Config Mode. Connect to: " + apSSID);
  Serial.println("Open: http://" + IP.toString());
}

// ---------- MQTT Callback ----------
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("Incoming topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(msg);

  String topicStr = String(topic);

  // Check if topic starts with your base path
  if (topicStr.startsWith("smartHome/device_1/relay")) {
    // Extract relay number
    int relayNum = topicStr.substring(strlen("smartHome/device_1/relay")).toInt();

    if (relayNum >= 1 && relayNum <= 8) {
      // Update the actual relay
      int relayPin = relayPins[relayNum - 1];
      if (msg == "ON") {
        digitalWrite(relayPin, LOW);  // Active LOW
      } else if (msg == "OFF") {
        digitalWrite(relayPin, HIGH);
      }

      Serial.printf("Relay %d set to %s\n", relayNum, msg.c_str());
    }
  }
}



void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect(mqtt_user, mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      client.subscribe((baseTopic + "#").c_str());
    } else {
      Serial.print("failed (");
      Serial.print(client.state());
      Serial.println("), retrying in 5s");
      delay(5000);
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

for (int i = 0; i < 8; i++) {
  pinMode(relayPins[i], OUTPUT);
  digitalWrite(relayPins[i], HIGH); // OFF by default (active LOW)

  pinMode(switchPins[i], INPUT_PULLUP);
  lastSwitchStates[i] = digitalRead(switchPins[i]);
}


  if (!connectToWiFiFromEEPROM()) {
    startAPMode();
  } else {
    Serial.println("Connected to WiFi: " + WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
  }
}

// ---------- Loop ----------
void loop() {
  dnsServer.processNextRequest();

for (int i = 0; i < 8; i++) {
  int currentState = digitalRead(switchPins[i]);
  if (lastSwitchStates[i] == HIGH && currentState == LOW) {
    int relayPin = relayPins[i];
    bool currentRelayState = digitalRead(relayPin); // HIGH = OFF
    digitalWrite(relayPin, !currentRelayState); // Toggle

    // Build topic like: smartHome/device_1/relay1=ON
    String topic = baseTopic + "relay" + String(i + 1) + "=" + (!currentRelayState ? "ON" : "OFF");

    if (client.connected()) {
      client.publish(topic.c_str(), "");
    }

    Serial.printf("Manual Toggle: Relay %d → %s\n", i + 1, (!currentRelayState ? "ON" : "OFF"));

    delay(300);  // Debounce
  }
  lastSwitchStates[i] = currentState;
}




  if (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
  } else {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }
}
