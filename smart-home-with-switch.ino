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
const char* mqtt_user = "Byte4geSmart";
const char* mqtt_pass = "Byte4geSmart@7634";

const char* device_id = "ESP32-B4G01"; 

String baseTopic = "smartHome/" + String(device_id) + "/";

// ---------- Relay Pins ----------


int relayPins[4] = { 5, 18, 19, 26 }; 
int switchPins[4] = { 4, 16, 17, 25 };  
int lastSwitchStates[4];

#define RESET_BUTTON_PIN 32  // Define the reset button pin


unsigned long lastRSSIPublish = 0;
const unsigned long rssiInterval = 10000;  // 10 seconds

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


void resetWiFiCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Reset SSID and password in EEPROM to empty values
  for (int i = 0; i < 32; i++) EEPROM.write(i, 0);  // Clear SSID
  for (int i = 0; i < 64; i++) EEPROM.write(32 + i, 0);  // Clear password
  
  EEPROM.commit();  // Save changes to EEPROM
  Serial.println("Wi-Fi credentials reset!");
}


// ---------- Web Config ----------
void handleRoot() {
  String html = R"====(
  <!DOCTYPE html><html lang='en'><head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>BYTE4GE SMART HOME</title>
    <style>
      body {
        background-color: #121212; color: #fff; font-family: Arial, sans-serif;
        margin: 0; padding: 0; display: flex; justify-content: center;
        align-items: center; height: 100vh; flex-direction: column;
      }
      h2 { color: #f39c12; font-size: 2rem; margin-bottom: 20px; }
       p { color: #f39c12;}
      form {
        background-color: #333; padding: 20px; border-radius: 8px;
        box-shadow: 0 4px 8px rgba(0,0,0,0.2); width: 100%; max-width: 400px;
        margin-top: 20px;
      }
      label { display: block; margin-bottom: 8px; }
      select, input[type='password'] {
        background-color: #444; border: none; color: #fff;
        padding: 10px; border-radius: 4px; width: 100%;
        margin-bottom: 16px; font-size: 1rem;
      }
      input[type='submit'] {
        background-color: #f39c12; color: #fff; border: none;
        padding: 10px 20px; border-radius: 4px; cursor: pointer;
        width: 100%; font-size: 1rem; margin-top: 20px;
      }
      .loader {
        border: 4px solid #f3f3f3;
        border-top: 4px solid #3498db;
        border-radius: 50%;
        width: 40px;
        height: 40px;
        animation: spin 1.5s linear infinite;
        margin: 20px auto;
      }
      
    </style>
  </head><body>
    <h2>CONNECT TO WIFI</h2>
    <form action='/save' method='post' onsubmit='showLoader()'>
      <label for='ssid'>Select Network:</label>
      <select name='ssid' id='ssid'><option>Loading...</option></select>
      <label for='pass'>Password:</label>
      <input name='pass' type='password'>
      <input type='submit' value='Save & Reboot'>
    </form>
    <p>Copyright Ⓒ 2023-24 Byte4ge (Sardar Enterprises) All Rights Reserved.</p>
   
    <script>
   
      async function loadNetworks() {
        const res = await fetch('/networks');
        const networks = await res.json();
        const select = document.getElementById('ssid');
        select.innerHTML = '';
        networks.forEach(ssid => {
          const opt = document.createElement('option');
          opt.value = ssid;
          opt.textContent = ssid;
          select.appendChild(opt);
        });
      }

      loadNetworks();
    </script>
  </body></html>
  )====";

  server.send(200, "text/html", html);
}


void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    // Save credentials to EEPROM (optional)
    saveWiFiCredentials(ssid, pass);
    
    // Attempt to connect to the chosen Wi-Fi network
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait for connection
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi!");
      server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      background-color: #121212;
      color: #ffffff;
      font-family: 'Segoe UI', sans-serif;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100vh;
      margin: 0;
      padding: 0;
    }

    .message-box {
      background-color: #1f1f1f;
      padding: 30px;
      border-radius: 12px;
      box-shadow: 0 0 10px rgba(255, 255, 255, 0.1);
      text-align: center;
    }

    .message-box h3 {
      margin-bottom: 20px;
    }

    .close-btn {
      background-color: #ff4c4c;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 8px;
      font-size: 16px;
      cursor: pointer;
    }

    .close-btn:hover {
      background-color: #ff1c1c;
    }

    @media (max-width: 600px) {
      .message-box {
        width: 90%;
      }
    }
  </style>
  <script>
    setTimeout(() => window.close(), 5000);
  </script>
</head>
<body>
  <div class="message-box">
    <h3>🎉 Connected! Rebooting...</h3>
    <button class="close-btn" onclick="window.close()">Close</button>
  </div>
</body>
</html>
)rawliteral");

      delay(2000);
      ESP.restart();
    } else {
      server.send(400, "text/html", "<h3>Failed to connect to Wi-Fi!</h3>");
    }
  } else {
    server.send(400, "text/plain", "Missing SSID or Password");
  }
}


void handleNetworks() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    json += "\"" + WiFi.SSID(i) + "\"";
    if (i < n - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}


void startAPMode() {
  String apSSID = "SMART_HOME_" + String(device_id);
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
// Callback when MQTT message is received
void callback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String msg;

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("Received topic: " + topicStr);
  Serial.println("Message: " + msg);

  // Check if the topic starts with the expected base
  if (topicStr.startsWith(baseTopic)) {
    String subTopic = topicStr.substring(baseTopic.length());  // e.g., relay1
    if (subTopic.startsWith("relay")) {
      int relayNum = subTopic.substring(5).toInt();  // Get the number after 'relay'
      if (relayNum >= 1 && relayNum <= 4) {
        int relayPin = relayPins[relayNum - 1];
        if (msg == "ON") {
          digitalWrite(relayPin, LOW);  // Active LOW
        } else if (msg == "OFF") {
          digitalWrite(relayPin, HIGH);
        }

        Serial.printf("Relay %d set to %s\n", relayNum, msg.c_str());
      } else {
        Serial.println("Invalid relay number");
      }
    }
  } else {
    Serial.println("MQTT topic not matching base topic");
  }
}



void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");

    String statusTopic = baseTopic + "status";

    // Connect with LWT
    if (client.connect(
          device_id,               // Client ID
          mqtt_user, mqtt_pass,    // Username and password
          statusTopic.c_str(),     // LWT topic
          0,                       // QoS
          true,                    // Retain flag
          "offline"                // LWT message
        )) {
      
      Serial.println("connected");

      // Publish online status
      client.publish(statusTopic.c_str(), "online", true);  // Retained

      // Subscribe to relevant topics
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

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // OFF by default (active LOW)

    pinMode(switchPins[i], INPUT_PULLUP);
    lastSwitchStates[i] = digitalRead(switchPins[i]);
  }
 pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

server.on("/", handleRoot);
server.on("/networks", handleNetworks);


  if (!connectToWiFiFromEEPROM()) {
    startAPMode();
  } else {
    Serial.println("Connected to WiFi: " + WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, mqtt_port);   

String statusTopic = baseTopic + "status";

// Set the callback BEFORE anything else
client.setCallback(callback);

// Attempt to connect with LWT
if (client.connect(
      device_id,
      mqtt_user, mqtt_pass,
      statusTopic.c_str(), 0, true, "offline")) {

  Serial.println("MQTT connected!");

  // Publish online status
  client.publish(statusTopic.c_str(), "online", true);

String IPtopic = baseTopic + "ipAddress";

// Convert IP to string and then to char array
String ipStr = WiFi.localIP().toString();
client.publish(IPtopic.c_str(), ipStr.c_str(), true); // 'true' makes it a retained message


  // Subscribe to all relevant topics under baseTopic
  client.subscribe((baseTopic + "#").c_str());

} else {
  Serial.print("MQTT connection failed. State: ");
  Serial.println(client.state());
}

  }
}

// ---------- Loop ----------
void loop() {
  dnsServer.processNextRequest();
  for (int i = 0; i < 4; i++) {
    int currentState = digitalRead(switchPins[i]);

    // Check if the switch state has changed (debounced)
    if (lastSwitchStates[i] == HIGH && currentState == LOW) {
      // Build topic like: smartHome/device_1/relay1
      String topic = baseTopic + "relay" + String(i + 1);

      digitalWrite(relayPins[i], LOW);
      // Send the state of the switch to the MQTT broker
      if (client.connected()) {
        client.publish(topic.c_str(), "ON", true); // Retained = true
      }

      // Print to Serial for debugging
      Serial.printf("Manual Update: Relay %d → OFF\n", i + 1);

      delay(300);  // Debounce delay
    }
    // If the switch is ON (HIGH), send ON to MQTT
    else if (lastSwitchStates[i] == LOW && currentState == HIGH) {
      // Build topic like: smartHome/device_1/relay1
      String topic = baseTopic + "relay" + String(i + 1);


      digitalWrite(relayPins[i], HIGH);
      // Send the state of the switch to the MQTT broker
      if (client.connected()) {
        client.publish(topic.c_str(), "OFF", true);
      }

      // Print to Serial for debugging
      Serial.printf("Manual Update: Relay %d → ON\n", i + 1);

      delay(300);  // Debounce delay
    }

    // Update the last switch state for the next iteration
    lastSwitchStates[i] = currentState;
  }


if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    resetWiFiCredentials();
    delay(1000);  // Debounce and avoid multiple triggers
    ESP.restart();  // Reboot to enter AP mode for Wi-Fi configuration
  }

  if (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
  } else {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }
  
unsigned long now = millis();
  if (now - lastRSSIPublish > rssiInterval) {
    lastRSSIPublish = now;

    long rssi = WiFi.RSSI();
    String rssiStr = String(rssi);
    String Wifidbm = baseTopic + "Wifidbm";
    client.publish(Wifidbm.c_str(), rssiStr.c_str(), true);

    Serial.println("Published WiFi RSSI: " + rssiStr);
  }

}
