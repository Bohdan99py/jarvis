// ============================================================
// esp32_jarvis_node.ino — J.A.R.V.I.S. Physical Node Firmware
//
// Turns a bare ESP32 into a Jarvis peripheral:
//   - WiFi HTTP server (REST API for status/commands)
//   - Serial JSON protocol (USB fallback)
//   - Built-in LED notifications (patterns via PWM)
//   - Capacitive touch pin (button without hardware)
//   - Internal temperature + Hall effect sensor
//
// No extra hardware required — works on any ESP32 dev board.
//
// HTTP API:
//   GET  /ping     → heartbeat
//   GET  /status   → sensor snapshot (temp, hall, touch, heap, rssi)
//   POST /led      → set LED pattern {"mode":"breathe","speed":1000}
//   POST /notify   → notification pulse {"type":"info","duration":3000}
//
// Serial protocol (115200 baud, JSON lines):
//   → {"cmd":"status"}              ← {"temp":42.5,"hall":30,...}
//   → {"cmd":"led","mode":"blink"}  ← {"ok":true}
//   ESP32 pushes events:            ← {"event":"touch","pin":4}
//                                   ← {"event":"hall","value":500}
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();  // internal temp sensor (ROM function)
#ifdef __cplusplus
}
#endif

// ── Pin Definitions ──────────────────────────────────────────
static const int LED_PIN   = 2;    // built-in LED on most ESP32 boards
static const int TOUCH_PIN = 4;    // T0 = GPIO4 (touch a wire/jumper)
static const int LED_CHANNEL = 0;  // LEDC PWM channel
static const int LED_FREQ    = 5000;
static const int LED_RES     = 8;  // 8-bit resolution (0-255)

// ── Touch Threshold ──────────────────────────────────────────
static const int TOUCH_THRESHOLD  = 40;  // below this = touched
static const unsigned long TOUCH_DEBOUNCE_MS = 300;
static const unsigned long DOUBLE_TOUCH_MS   = 500;

// ── Hall Sensor ──────────────────────────────────────────────
static const int HALL_CHANGE_THRESHOLD = 50; // significant delta
static const unsigned long HALL_POLL_MS = 1000;

// ── Heartbeat (mDNS/callback) ────────────────────────────────
static const unsigned long HEARTBEAT_MS = 30000;

// ── LED Pattern Modes ────────────────────────────────────────
enum LedMode {
    LED_OFF,
    LED_SOLID,
    LED_BLINK,
    LED_BREATHE,
    LED_PULSE,
    LED_SOS,
    LED_THINKING
};

// ── Global State ─────────────────────────────────────────────
WebServer server(80);
Preferences prefs;

// WiFi config (stored in NVS)
String wifiSsid;
String wifiPass;
String jarvisIp;    // Jarvis PC IP for callbacks
int    jarvisPort = 0;

// LED state
LedMode  ledMode  = LED_BREATHE;
int      ledSpeed = 1000;  // ms for one cycle
unsigned long ledTimer = 0;
int      ledPhase = 0;     // sub-state for patterns

// Notification overlay
bool     notifyActive = false;
String   notifyType;
unsigned long notifyEnd = 0;
LedMode  prevLedMode = LED_BREATHE;
int      prevLedSpeed = 1000;

// Touch
unsigned long lastTouchTime = 0;
bool     touchWasDown = false;
int      touchTapCount = 0;
unsigned long lastTapTime = 0;

// Hall
int      lastHallValue = 0;

// Timers
unsigned long lastHeartbeat = 0;
unsigned long lastHallPoll  = 0;
unsigned long lastStatusPush = 0;

// ── Forward Declarations ─────────────────────────────────────
void setupWiFi();
void setupServer();
void setupLed();
void handleSerialInput();
void updateLed();
void pollTouch();
void pollHall();
void sendHeartbeat();
String buildStatusJson();
void setLedPattern(LedMode mode, int speed);
void sendSerialEvent(const String& eventJson);
void sendCallbackToJarvis(const String& eventType, const String& payload);

// ── SETUP ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== J.A.R.V.I.S. ESP32 Node ==="));
    Serial.println(F("Firmware v1.0.0"));

    // Load WiFi credentials from NVS
    prefs.begin("jarvis", false);
    wifiSsid   = prefs.getString("ssid", "");
    wifiPass   = prefs.getString("pass", "");
    jarvisIp   = prefs.getString("jarvis_ip", "");
    jarvisPort = prefs.getInt("jarvis_port", 0);
    prefs.end();

    setupLed();
    setupWiFi();
    setupServer();

    lastHallValue = hallRead();

    Serial.println(F("Node ready. Awaiting commands."));
    Serial.println("{\"event\":\"boot\",\"version\":\"1.0.0\"}");
}

// ── LOOP ─────────────────────────────────────────────────────
void loop() {
    server.handleClient();
    handleSerialInput();
    updateLed();
    pollTouch();
    pollHall();

    // Notification timeout
    if (notifyActive && millis() > notifyEnd) {
        notifyActive = false;
        ledMode  = prevLedMode;
        ledSpeed = prevLedSpeed;
    }

    // Periodic heartbeat
    if (millis() - lastHeartbeat > HEARTBEAT_MS) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    delay(10);
}

// ── WiFi Setup ───────────────────────────────────────────────
void setupWiFi() {
    if (wifiSsid.isEmpty()) {
        Serial.println(F("No WiFi configured. Use Serial to set:"));
        Serial.println(F("  {\"cmd\":\"config\",\"ssid\":\"...\",\"pass\":\"...\"}"));
        setLedPattern(LED_SOS, 300);
        return;
    }

    Serial.printf("Connecting to WiFi: %s\n", wifiSsid.c_str());
    setLedPattern(LED_THINKING, 200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("{\"event\":\"wifi\",\"ip\":\"%s\",\"rssi\":%d}\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        setLedPattern(LED_BREATHE, 2000);
    } else {
        Serial.println(F("WiFi connection failed."));
        Serial.println("{\"event\":\"wifi_fail\"}");
        setLedPattern(LED_SOS, 500);
    }
}

// ── LED Setup (LEDC PWM) ────────────────────────────────────
void setupLed() {
    ledcSetup(LED_CHANNEL, LED_FREQ, LED_RES);
    ledcAttachPin(LED_PIN, LED_CHANNEL);
    ledcWrite(LED_CHANNEL, 0);
}

void setLedBrightness(int val) {
    ledcWrite(LED_CHANNEL, constrain(val, 0, 255));
}

// ── LED Pattern Engine ──────────────────────────────────────
void setLedPattern(LedMode mode, int speed) {
    ledMode  = mode;
    ledSpeed = max(speed, 50);
    ledTimer = millis();
    ledPhase = 0;
}

void updateLed() {
    unsigned long now = millis();
    unsigned long elapsed = now - ledTimer;

    LedMode activeMode = notifyActive ? ledModeForNotify(notifyType) : ledMode;
    int activeSpeed = notifyActive ? 150 : ledSpeed;

    switch (activeMode) {
    case LED_OFF:
        setLedBrightness(0);
        break;

    case LED_SOLID:
        setLedBrightness(255);
        break;

    case LED_BLINK: {
        bool on = (elapsed % (unsigned long)activeSpeed) < ((unsigned long)activeSpeed / 2);
        setLedBrightness(on ? 255 : 0);
        break;
    }

    case LED_BREATHE: {
        float phase = (float)(elapsed % (unsigned long)activeSpeed) / (float)activeSpeed;
        float val = (sin(phase * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
        setLedBrightness((int)(val * 255));
        break;
    }

    case LED_PULSE: {
        int cycleMs = 200;
        int pulseNum = (elapsed / cycleMs) / 2;
        if (pulseNum < 3) {
            bool on = ((elapsed / cycleMs) % 2) == 0;
            setLedBrightness(on ? 255 : 0);
        } else {
            setLedBrightness(0);
            if (!notifyActive) {
                ledMode = LED_BREATHE;
                ledSpeed = 2000;
                ledTimer = millis();
            }
        }
        break;
    }

    case LED_SOS: {
        // ... --- ...  (S=short O=long)
        const int pattern[] = {1,0,1,0,1,0,0, 2,0,2,0,2,0,0, 1,0,1,0,1,0,0,0};
        const int patLen = 22;
        int unit = activeSpeed;
        unsigned long total = 0;
        int idx = 0;
        unsigned long rem = elapsed % (unsigned long)(15 * unit);
        for (int i = 0; i < patLen; i++) {
            unsigned long dur = pattern[i] * unit;
            if (rem < total + dur) { idx = i; break; }
            total += dur;
            idx = i;
        }
        setLedBrightness((idx % 2 == 0 && pattern[idx] > 0) ? 255 : 0);
        break;
    }

    case LED_THINKING: {
        // Fast irregular flicker
        int r = (int)(sin(elapsed * 0.013f) * 127 + 128);
        r = (r * (int)(sin(elapsed * 0.037f) * 50 + 70)) / 100;
        setLedBrightness(constrain(r, 0, 255));
        break;
    }
    }
}

LedMode ledModeForNotify(const String& type) {
    if (type == "error")    return LED_SOS;
    if (type == "thinking") return LED_THINKING;
    if (type == "warning")  return LED_BLINK;
    return LED_PULSE; // "info" and default
}

// ── HTTP Server ─────────────────────────────────────────────
void setupServer() {
    // GET /ping
    server.on("/ping", HTTP_GET, []() {
        StaticJsonDocument<128> doc;
        doc["status"] = "ok";
        doc["uptime"] = millis() / 1000;
        doc["node"]   = "jarvis_esp32";
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // GET /status
    server.on("/status", HTTP_GET, []() {
        server.send(200, "application/json", buildStatusJson());
    });

    // POST /led
    server.on("/led", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"no body\"}");
            return;
        }
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }

        String mode = doc["mode"] | "off";
        int speed   = doc["speed"] | 1000;

        LedMode m = LED_OFF;
        if      (mode == "solid")    m = LED_SOLID;
        else if (mode == "blink")    m = LED_BLINK;
        else if (mode == "breathe")  m = LED_BREATHE;
        else if (mode == "pulse")    m = LED_PULSE;
        else if (mode == "sos")      m = LED_SOS;
        else if (mode == "thinking") m = LED_THINKING;

        notifyActive = false;
        setLedPattern(m, speed);
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // POST /notify
    server.on("/notify", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"no body\"}");
            return;
        }
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }

        notifyType  = doc["type"] | "info";
        int dur     = doc["duration"] | 3000;
        prevLedMode  = ledMode;
        prevLedSpeed = ledSpeed;
        notifyActive = true;
        notifyEnd    = millis() + dur;
        ledTimer     = millis();

        server.send(200, "application/json", "{\"ok\":true}");
    });

    // GET /config
    server.on("/config", HTTP_GET, []() {
        StaticJsonDocument<256> doc;
        doc["ssid"]        = wifiSsid;
        doc["jarvis_ip"]   = jarvisIp;
        doc["jarvis_port"] = jarvisPort;
        doc["ip"]          = WiFi.localIP().toString();
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // POST /config
    server.on("/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"no body\"}");
            return;
        }
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }

        bool needReconnect = false;
        if (doc.containsKey("ssid")) {
            wifiSsid = doc["ssid"].as<String>();
            needReconnect = true;
        }
        if (doc.containsKey("pass")) {
            wifiPass = doc["pass"].as<String>();
            needReconnect = true;
        }
        if (doc.containsKey("jarvis_ip"))
            jarvisIp = doc["jarvis_ip"].as<String>();
        if (doc.containsKey("jarvis_port"))
            jarvisPort = doc["jarvis_port"] | 0;

        // Persist to NVS
        prefs.begin("jarvis", false);
        prefs.putString("ssid", wifiSsid);
        prefs.putString("pass", wifiPass);
        prefs.putString("jarvis_ip", jarvisIp);
        prefs.putInt("jarvis_port", jarvisPort);
        prefs.end();

        server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");

        if (needReconnect) {
            delay(500);
            ESP.restart();
        }
    });

    server.begin();
    Serial.println(F("HTTP server started on port 80"));
}

// ── Status JSON Builder ─────────────────────────────────────
String buildStatusJson() {
    StaticJsonDocument<512> doc;
    doc["node"]    = "jarvis_esp32";
    doc["version"] = "1.0.0";
    doc["uptime"]  = millis() / 1000;
    doc["heap"]    = ESP.getFreeHeap();

    // Internal temperature (Fahrenheit from ROM, convert to Celsius)
    float tempF = temprature_sens_read();
    float tempC = (tempF - 32.0f) / 1.8f;
    doc["temp_c"]  = round(tempC * 10.0f) / 10.0f;

    // Hall effect sensor
    doc["hall"]    = hallRead();

    // Touch pin
    int touchVal = touchRead(TOUCH_PIN);
    doc["touch_raw"] = touchVal;
    doc["touch"]     = (touchVal < TOUCH_THRESHOLD);

    // WiFi
    if (WiFi.status() == WL_CONNECTED) {
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["ip"]        = WiFi.localIP().toString();
    } else {
        doc["wifi_rssi"] = 0;
        doc["ip"]        = "";
    }

    // LED state
    const char* modes[] = {"off","solid","blink","breathe","pulse","sos","thinking"};
    doc["led_mode"] = modes[ledMode];

    String out;
    serializeJson(doc, out);
    return out;
}

// ── Serial Command Handler ──────────────────────────────────
static String serialBuffer;

void handleSerialInput() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            serialBuffer.trim();
            if (serialBuffer.length() > 0) {
                processSerialCommand(serialBuffer);
                serialBuffer = "";
            }
        } else {
            serialBuffer += c;
            if (serialBuffer.length() > 1024) serialBuffer = "";
        }
    }
}

void processSerialCommand(const String& line) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.println("{\"error\":\"bad json\"}");
        return;
    }

    String cmd = doc["cmd"] | "";

    if (cmd == "status") {
        Serial.println(buildStatusJson());
    }
    else if (cmd == "ping") {
        Serial.println("{\"status\":\"ok\",\"node\":\"jarvis_esp32\"}");
    }
    else if (cmd == "led") {
        String mode = doc["mode"] | "off";
        int speed   = doc["speed"] | 1000;

        LedMode m = LED_OFF;
        if      (mode == "solid")    m = LED_SOLID;
        else if (mode == "blink")    m = LED_BLINK;
        else if (mode == "breathe")  m = LED_BREATHE;
        else if (mode == "pulse")    m = LED_PULSE;
        else if (mode == "sos")      m = LED_SOS;
        else if (mode == "thinking") m = LED_THINKING;

        notifyActive = false;
        setLedPattern(m, speed);
        Serial.println("{\"ok\":true}");
    }
    else if (cmd == "notify") {
        notifyType  = doc["type"] | "info";
        int dur     = doc["duration"] | 3000;
        prevLedMode  = ledMode;
        prevLedSpeed = ledSpeed;
        notifyActive = true;
        notifyEnd    = millis() + dur;
        ledTimer     = millis();
        Serial.println("{\"ok\":true}");
    }
    else if (cmd == "config") {
        bool changed = false;
        if (doc.containsKey("ssid")) {
            wifiSsid = doc["ssid"].as<String>();
            changed = true;
        }
        if (doc.containsKey("pass")) {
            wifiPass = doc["pass"].as<String>();
            changed = true;
        }
        if (doc.containsKey("jarvis_ip"))
            jarvisIp = doc["jarvis_ip"].as<String>();
        if (doc.containsKey("jarvis_port"))
            jarvisPort = doc["jarvis_port"] | 0;

        prefs.begin("jarvis", false);
        prefs.putString("ssid", wifiSsid);
        prefs.putString("pass", wifiPass);
        prefs.putString("jarvis_ip", jarvisIp);
        prefs.putInt("jarvis_port", jarvisPort);
        prefs.end();

        Serial.println("{\"ok\":true}");
        if (changed) {
            Serial.println("{\"event\":\"restarting\"}");
            delay(500);
            ESP.restart();
        }
    }
    else if (cmd == "restart") {
        Serial.println("{\"ok\":true,\"event\":\"restarting\"}");
        delay(200);
        ESP.restart();
    }
    else {
        Serial.println("{\"error\":\"unknown cmd\"}");
    }
}

// ── Touch Polling ───────────────────────────────────────────
void pollTouch() {
    int val = touchRead(TOUCH_PIN);
    bool down = (val < TOUCH_THRESHOLD);
    unsigned long now = millis();

    if (down && !touchWasDown && (now - lastTouchTime > TOUCH_DEBOUNCE_MS)) {
        lastTouchTime = now;
        touchTapCount++;
        lastTapTime = now;
    }
    touchWasDown = down;

    // Emit tap event after double-touch window expires
    if (touchTapCount > 0 && (now - lastTapTime > DOUBLE_TOUCH_MS)) {
        StaticJsonDocument<128> doc;
        doc["event"] = "touch";
        doc["pin"]   = TOUCH_PIN;
        doc["taps"]  = touchTapCount;
        String out;
        serializeJson(doc, out);
        sendSerialEvent(out);
        sendCallbackToJarvis("touch", out);
        touchTapCount = 0;
    }
}

// ── Hall Effect Polling ─────────────────────────────────────
void pollHall() {
    unsigned long now = millis();
    if (now - lastHallPoll < HALL_POLL_MS) return;
    lastHallPoll = now;

    int val = hallRead();
    int delta = abs(val - lastHallValue);

    if (delta > HALL_CHANGE_THRESHOLD) {
        lastHallValue = val;
        StaticJsonDocument<128> doc;
        doc["event"] = "hall";
        doc["value"] = val;
        doc["delta"] = delta;
        String out;
        serializeJson(doc, out);
        sendSerialEvent(out);
        sendCallbackToJarvis("hall", out);
    }
}

// ── Serial Event Push ───────────────────────────────────────
void sendSerialEvent(const String& eventJson) {
    Serial.println(eventJson);
}

// ── HTTP Callback to Jarvis ─────────────────────────────────
void sendCallbackToJarvis(const String& eventType, const String& payload) {
    if (jarvisIp.isEmpty() || jarvisPort == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClient client;
    if (!client.connect(jarvisIp.c_str(), jarvisPort)) return;

    String path = "/esp32/event?type=" + eventType;
    client.printf("POST %s HTTP/1.1\r\n", path.c_str());
    client.printf("Host: %s:%d\r\n", jarvisIp.c_str(), jarvisPort);
    client.println("Content-Type: application/json");
    client.printf("Content-Length: %d\r\n", payload.length());
    client.println("Connection: close");
    client.println();
    client.print(payload);
    client.stop();
}

// ── Heartbeat ───────────────────────────────────────────────
void sendHeartbeat() {
    // Serial heartbeat
    StaticJsonDocument<128> doc;
    doc["event"]  = "heartbeat";
    doc["uptime"] = millis() / 1000;
    doc["heap"]   = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    Serial.println(out);

    // HTTP callback heartbeat
    sendCallbackToJarvis("heartbeat", out);
}
