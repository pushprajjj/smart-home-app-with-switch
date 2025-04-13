#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <DNSServer.h>


DNSServer dnsServer;
const byte DNS_PORT = 53;
 

// ---------- Config ----------
#define EEPROM_SIZE 96
const char* mqtt_server = "server.iistbihar.com"; // Change to your MQTT broker IP
const int mqtt_port = 1883;

const char* mqtt_user = "device_1";
const char* mqtt_pass = "testpass";

String baseTopic = "smartHome/" + String(mqtt_user) + "/";

// ---------- Relay Pins ----------
int relayPins[8] = {16, 17, 18, 19, 21, 22, 23, 25};


// ---------- Objects ----------
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);

// ---------- Functions ----------
void saveWiFiCredentials(String ssid, String pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) EEPROM.write(i, i < ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < 64; i++) EEPROM.write(32 + i, i < pass.length() ? pass[i] : 0);
  EEPROM.commit();
}

void loadWiFiCredentials(char* ssid, char* pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 32; i++) ssid[i] = EEPROM.read(i);
  ssid[32] = '\0'; // Ensure null-terminated
  for (int i = 0; i < 64; i++) pass[i] = EEPROM.read(32 + i);
  pass[64] = '\0'; // Ensure null-terminated
}


bool connectToWiFiFromEEPROM() {
  char ssid[33] = {0};
  char pass[65] = {0};
  loadWiFiCredentials(ssid, pass);
  if (strlen(ssid) == 0) return false;

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

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
  String apSSID = "SMART_HOME_" + String(mqtt_user);  // Concatenate properly
  WiFi.softAP(apSSID.c_str());  // Convert to C-string

  IPAddress IP = WiFi.softAPIP();

  dnsServer.start(DNS_PORT, "*", IP);  // Redirect all domains to ESP32 IP

  Serial.println("AP Mode - Connect to SMART_HOME_device_1");
  Serial.print("Go to http://");
  Serial.println(IP);

  // Also ensure your server handles requests
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  
  // Reiterate the current status and remind user on the serial monitor
  Serial.println("Wi-Fi Configuration mode is active. Connect to the network with SSID: " + apSSID);
}



void callback(char* topic, byte* message, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)message[i];

  String statusTopic = baseTopic + "status"; // Topic for relay statuses

  // Check if the received topic is the status topic
  if (String(topic) == statusTopic) {
    // Split the message into relay states
    int relayIndex = 0;
    int startIndex = 0;
    for (int i = 0; i <= msg.length(); i++) {
      if (msg.charAt(i) == ',' || i == msg.length()) {
        String state = msg.substring(startIndex, i);
        if (relayIndex < 8) {
          // Update the relay state based on the received "ON" or "OFF" message
          digitalWrite(relayPins[relayIndex], state == "ON" ? LOW : HIGH);
          relayIndex++;
        }
        startIndex = i + 1;
      }
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_user, mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      for (int i = 0; i < 8; i++) {
        client.subscribe((baseTopic + String(i + 1)).c_str());
      }
      client.subscribe((baseTopic + "status").c_str());  // Subscribe to the status topic
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void publishRelayStatus() {
  String relayState = "";
  for (int i = 0; i < 8; i++) {
    relayState += (digitalRead(relayPins[i]) == LOW ? "ON" : "OFF");
    if (i < 7) relayState += ",";  // Add comma between states
  }
  client.publish((baseTopic + "status").c_str(), relayState.c_str(), true);  // Publish with retention
}


// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  for (int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);
  }

  connectToWiFiFromEEPROM();  // Try connecting anyway

  if (WiFi.status() != WL_CONNECTED) {
    startAPMode(); // ✅ Go to AP Mode if WiFi isn't actually connected
  } else {
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
  }
}


void loop() {
  dnsServer.processNextRequest();

  if (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
  } else {
    if (!client.connected()) reconnectMQTT();
    client.loop();
    // Periodically publish relay states
    publishRelayStatus();
  }
}
