// ============================================================
//              HEALTH MONITORING SYSTEM USING ESP32
// ============================================================
// Features:
// - Heart Rate Monitoring
// - SpO2 Monitoring
// - Temperature Monitoring
// - Motion Detection using BMA400
// - OLED Display Output
// - Wi-Fi Web Monitoring
// - Data Structures Implementation
//      * Linked List
//      * Queue
//      * Circular Queue
// - I2C Bus Recovery Mechanism
// ============================================================

// ===================== LIBRARIES ============================

// I2C Communication Library
#include <Wire.h>

// MAX30102 Sensor Library
#include <DFRobot_BloodOxygen_S.h>

// BMA400 Accelerometer Library
#include <SparkFun_BMA400_Arduino_Library.h>

// OLED Display Libraries
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi & Web Server Libraries
#include <WiFi.h>
#include <WebServer.h>

// ===================== PIN DEFINITIONS ======================

// ESP32 I2C Pins
#define I2C_SDA       21
#define I2C_SCL       22

// I2C Addresses
#define MAX30102_ADDR 0x57
#define BMA400_ADDR1  0x14
#define BMA400_ADDR2  0x15

// OLED Display Configuration
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

// ===================== WIFI CONFIGURATION ===================

// Wi-Fi Access Point Credentials
const char* ssid     = "ESP32_Health";
const char* password = "12345678";

// Create Web Server on Port 80
WebServer server(80);

// ===================== SENSOR OBJECTS =======================

// MAX30102 Sensor Object
DFRobot_BloodOxygen_S_I2C MAX30102(&Wire, MAX30102_ADDR);

// BMA400 Accelerometer Object
BMA400 accelerometer;

// OLED Display Object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ============================================================
//                    DATA STRUCTURES
// ============================================================

// Structure to store all sensor values
struct SensorData {

  // Heart Rate Value
  int heartRate;

  // Blood Oxygen Level
  int spo2;

  // Temperature
  float temp;

  // Accelerometer Values
  float accelX, accelY, accelZ;

  // Timestamp
  unsigned long timestamp;
};

// ============================================================
// 1) LINKED LIST -> USED FOR COMPLETE SENSOR DATA HISTORY
// ============================================================

// Node Structure for Linked List
struct Node {

  // Sensor Data
  SensorData data;

  // Pointer to Next Node
  Node* next;
};

// Head Pointer
Node* logHead = nullptr;

// ============================================================
// 2) CIRCULAR QUEUE -> USED FOR RECENT SENSOR READINGS
// ============================================================

// Size of Rolling Buffer
#define RECENT_SIZE 10

struct CircQueue {

  // Buffer Array
  SensorData buf[RECENT_SIZE];

  // Queue Variables
  int front = 0, size = 0;

  // Function to Insert New Data
  void push(const SensorData& d) {

    // Calculate Index
    int idx = (front + size) % RECENT_SIZE;

    // If Queue Not Full
    if (size < RECENT_SIZE) {

      buf[idx] = d;
      size++;
    }

    // If Queue Full -> Overwrite Oldest Data
    else {

      buf[idx] = d;

      // Move Front Forward
      front = (front + 1) % RECENT_SIZE;
    }
  }

  // Function to Calculate Average Heart Rate
  float averageHR() const {

    // Return 0 if Queue Empty
    if (size == 0) return 0;

    long sum = 0;

    // Add All Heart Rate Values
    for (int i = 0; i < size; i++) {

      sum += buf[(front + i) % RECENT_SIZE].heartRate;
    }

    // Return Average
    return (float)sum / size;
  }

} recentQ;

// ============================================================
// 3) FIFO QUEUE -> USED FOR ALERT HISTORY STORAGE
// ============================================================

// Alert Node Structure
struct AlertNode {

  SensorData data;

  AlertNode* next;
};

// Alert Queue Structure
struct AlertQueue {

  AlertNode *head = nullptr, *tail = nullptr;

  // Function to Add Alert Data
  void enqueue(const SensorData& d) {

    auto* n = new AlertNode{d, nullptr};

    // If Queue Empty
    if (!tail)
      head = tail = n;

    // Add at End
    else {

      tail->next = n;
      tail = n;
    }
  }

  // Traverse Entire Queue
  template<typename F>
  void forEach(F fn) const {

    for (auto* p = head; p; p = p->next)
      fn(p->data);
  }

} alertQ;

// ============================================================
//        FUNCTION TO INSERT DATA INTO LINKED LIST
// ============================================================

void insertLog(const SensorData& d) {

  // Create New Node
  auto* n = new Node{ d, nullptr };

  // If List Empty
  if (!logHead)
    logHead = n;

  // Insert at Tail
  else {

    Node* t = logHead;

    while (t->next)
      t = t->next;

    t->next = n;
  }
}

// ============================================================
//                 I2C BUS RECOVERY FUNCTION
// ============================================================

// Counter for Recovery Attempts
unsigned long i2cRecoveryCount = 0;

void recoverI2CBus() {

  i2cRecoveryCount++;

  // Stop I2C
  Wire.end();

  // Set Pins as Pullup
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);

  // Generate Clock Pulses
  for (int i = 0; i < 10; i++) {

    pinMode(I2C_SCL, OUTPUT);

    digitalWrite(I2C_SCL, LOW);
    delayMicroseconds(5);

    digitalWrite(I2C_SCL, HIGH);

    pinMode(I2C_SCL, INPUT_PULLUP);

    delayMicroseconds(5);
  }

  // Restart I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Set Clock Speed
  Wire.setClock(100000);
}

// ============================================================
//                INITIALIZE BMA400 SENSOR
// ============================================================

bool initBMA400() {

  return accelerometer.beginI2C(BMA400_ADDR1) == BMA400_OK ||

         accelerometer.beginI2C(BMA400_ADDR2) == BMA400_OK;
}

// ============================================================
//               INITIALIZE MAX30102 SENSOR
// ============================================================

bool initMAX30102() {

  // Check Sensor Connection
  if (!MAX30102.begin())
    return false;

  // Start Sensor Data Collection
  MAX30102.sensorStartCollect();

  return true;
}

// ============================================================
//              DISPLAY SENSOR DATA ON OLED
// ============================================================

void displayData(const SensorData& d) {

  // Clear OLED Screen
  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0,0);

  // Print Sensor Values
  display.printf(
    "HR:%d bpm\nSpO2:%d%%\nT:%.2fC\nX:%.2f Y:%.2f\nZ:%.2f\n",

    d.heartRate,
    d.spo2,
    d.temp,
    d.accelX,
    d.accelY,
    d.accelZ
  );

  // Update Display
  display.display();
}

// ============================================================
//                ALERT FUNCTION FOR HIGH HR
// ============================================================

void triggerAlarm(const SensorData& d) {

  // Print Alert in Serial Monitor
  Serial.println("⚠ ALERT: High HR!");

  // Clear OLED
  display.clearDisplay();

  // Display Warning
  display.setTextSize(2);

  display.setCursor(0,0);

  display.print("High HR!");

  // Display Heart Rate
  display.setTextSize(1);

  display.setCursor(0,30);

  display.printf("%dbpm @%lums", d.heartRate,d.timestamp);

  display.display();

  delay(1500);
}

// ============================================================
//                  GENERATE WEB PAGE
// ============================================================

String generateLivePage() {

  String p =
  "<html><head><meta http-equiv='refresh' content='2'/>"
  "<title>ESP32 Health</title></head><body>";

  p += "<h2>Live (last 10)</h2>";

  p += "<p><a href='/history'>History</a> | "
       "<a href='/alerts'>Alerts</a></p>";

  p += "<table border='1'><tr>"
       "<th>t(ms)</th><th>HR</th>"
       "<th>SpO2</th><th>T</th></tr>";

  // Display Recent Queue Data
  for (int i = 0; i < recentQ.size; i++) {

    auto& d = recentQ.buf[
      (recentQ.front + i) % RECENT_SIZE
    ];

    p += "<tr><td>" + String(d.timestamp) +
         "</td><td>" + String(d.heartRate) +
         "</td><td>" + String(d.spo2) +
         "</td><td>" + String(d.temp,2) +
         "</td></tr>";
  }

  p += "</table></body></html>";

  return p;
}

// ============================================================
//                  WEB SERVER HANDLERS
// ============================================================

// Home Page
void handleRoot() {

  server.send(200,"text/html",generateLivePage());
}

// History Page
void handleHistory() {

  String h =
  "<html><body><h3>All History</h3><pre>";

  // Traverse Linked List
  for (auto* n = logHead; n; n = n->next) {

    auto& d = n->data;

    h += "[" + String(d.timestamp) + "] "
         "HR:" + String(d.heartRate) +
         " SpO2:" + String(d.spo2) +
         " T:" + String(d.temp,2) + "\n";
  }

  h += "</pre><a href='/'>Back</a></body></html>";

  server.send(200,"text/html",h);
}

// Alerts Page
void handleAlerts() {

  String h =
  "<html><body><h3>Alerts</h3><pre>";

  // Traverse Alert Queue
  alertQ.forEach([&](const SensorData& d){

    h += "[" + String(d.timestamp) + "] "
         "HR:" + String(d.heartRate) + "\n";
  });

  h += "</pre><a href='/'>Back</a></body></html>";

  server.send(200,"text/html",h);
}

// ============================================================
//                         SETUP FUNCTION
// ============================================================

void setup() {

  // Start Serial Communication
  Serial.begin(115200);

  // Initialize I2C
  Wire.begin(I2C_SDA,I2C_SCL);

  Wire.setClock(100000);

  // Initialize OLED Display
  if(!display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDR
    )) {

    while(1);
  }

  // Display Initialization Message
  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(0,0);

  display.print("Init...");

  display.display();

  // Initialize MAX30102
  if(!initMAX30102()) {

    recoverI2CBus();

    if(!initMAX30102())
      while(1);
  }

  // Initialize BMA400
  if(!initBMA400()) {

    recoverI2CBus();

    if(!initBMA400())
      while(1);
  }

  // Create ESP32 Wi-Fi Access Point
  WiFi.softAP(ssid,password);

  Serial.print("AP IP: ");

  Serial.println(WiFi.softAPIP());

  // Setup Web Routes
  server.on("/",       handleRoot);

  server.on("/history",handleHistory);

  server.on("/alerts", handleAlerts);

  // Start Server
  server.begin();

  // Ready Message
  display.clearDisplay();

  display.setCursor(0,0);

  display.print("Ready");

  display.display();
}

// ============================================================
//                          MAIN LOOP
// ============================================================

void loop() {

  // ========================================================
  // 1) READ SENSOR DATA
  // ========================================================

  // Read Heart Rate & SpO2
  MAX30102.getHeartbeatSPO2();

  // Create Sensor Data Object
  SensorData d;

  // Store Heart Rate
  d.heartRate =
  MAX30102._sHeartbeatSPO2.Heartbeat;

  // Store SpO2
  d.spo2 =
  MAX30102._sHeartbeatSPO2.SPO2;

  // Store Temperature
  d.temp =
  MAX30102.getTemperature_C();

  // Store Timestamp
  d.timestamp = millis();

  // Read Accelerometer Data
  if(accelerometer.getSensorData()
     == BMA400_OK) {

    d.accelX =
    accelerometer.data.accelX;

    d.accelY =
    accelerometer.data.accelY;

    d.accelZ =
    accelerometer.data.accelZ;
  }

  // ========================================================
  // 2) DATA STRUCTURE OPERATIONS
  // ========================================================

  // Insert into Linked List
  insertLog(d);

  // Insert into Circular Queue
  recentQ.push(d);

  // Calculate Average Heart Rate
  float avgHR =
  recentQ.averageHR();

  // ========================================================
  // 3) ALERT CHECKING
  // ========================================================

  // If Heart Rate High
  if (avgHR > 100) {

    // Store Alert
    alertQ.enqueue(d);

    // Trigger Warning
    triggerAlarm(d);
  }

  // Otherwise Show Normal Display
  else {

    displayData(d);
  }

  // ========================================================
  // 4) WEB SERVER + SERIAL DEBUG
  // ========================================================

  // Handle Client Requests
  server.handleClient();

  // Print Values in Serial Monitor
  Serial.printf(
    "HR:%d SpO2:%d T:%.2f avgHR:%.1f\n",

    d.heartRate,
    d.spo2,
    d.temp,
    avgHR
  );

  // Delay 1 Second
  delay(1000);
}