#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "WiFi.h"
#include "PubSubClient.h"
#include "time.h"

// Make sure you change #define MQTT_MAX_PACKET_SIZE 512 in PubSubClient.h

// User-preference variables. Change below to suit your personal/device preference. These settings are for the smartifier.gateway.
const char* wifiSSID   = "SSIDGOESHERE";
const char* wifiPass   = "PASSWORDGOESHERE";
const char* mqttServer = "MQTTSERVERGOESHERE"; // Use either ip address or fully-qualified domain name (domain searches do not work!)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 3600;
const int   scanTime = 8; //In seconds, I recommend against multiples of 5 because you might miss some periodic but regular beacons

#define SMARTIFIER "smartifier"
#define SMARTIFIERTYPE "Gateway"
#define SMARTIFIERVERSION "0.9a"

BLEScan* pBLEScan;
WiFiClient espClient;
PubSubClient client(espClient);
int heartbeat = 0;
bool boot = true;
struct tm timeinfo;
char mqttClientID[24];
char mqttTopic[128];
char mqttMessage[512];
char bleAddress[32];
char timeString[20];
char bootTime[20];
const int wdtTimeout = 60;  //time in sec to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() {
  ets_printf("Module hung. Rebooting...\n");
  esp_restart();
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    char* pHex;
    Serial.printf(".. Advertised Device: %s \n", advertisedDevice.toString().c_str());

    sprintf(bleAddress, "%s", advertisedDevice.getAddress().toString().c_str());
    sprintf(mqttMessage, "{\n  \"gateway\": \"%s\"", mqttClientID);
    sprintf(mqttMessage, "%s,\n  \"address\": \"%s\"", mqttMessage, bleAddress);
    sprintf(mqttMessage, "%s,\n  \"name\": \"%s\"", mqttMessage, advertisedDevice.getName().c_str());
    pHex = BLEUtils::buildHexData(nullptr, (uint8_t*)advertisedDevice.getName().data(), advertisedDevice.getName().length());
    sprintf(mqttMessage, "%s,\n  \"nameHex\": \"%s\"", mqttMessage, pHex);
    sprintf(mqttMessage, "%s,\n  \"rssi\": %d", mqttMessage, advertisedDevice.getRSSI());
    if (advertisedDevice.haveServiceUUID()) {
      sprintf(mqttMessage, "%s,\n  \"serviceUUID\": \"%s\"", mqttMessage, advertisedDevice.getServiceUUID().toString().c_str());
    }
    if (advertisedDevice.haveServiceData()) {
      pHex = BLEUtils::buildHexData(nullptr, (uint8_t*)advertisedDevice.getServiceData().data(), advertisedDevice.getServiceData().length());
      sprintf(mqttMessage, "%s,\n  \"serviceDataUUID\": \"%s\"", mqttMessage, advertisedDevice.getServiceDataUUID().toString().c_str());
      sprintf(mqttMessage, "%s,\n  \"serviceData\": \"%s\"", mqttMessage, pHex);
    }
    if (advertisedDevice.haveManufacturerData()) {
      pHex = BLEUtils::buildHexData(nullptr, (uint8_t*)advertisedDevice.getManufacturerData().data(), advertisedDevice.getManufacturerData().length());
      sprintf(mqttMessage, "%s,\n  \"data\": \"%s\"", mqttMessage, pHex);
    }
    if (advertisedDevice.haveTXPower()) {
      sprintf(mqttMessage, "%s,\n  \"txPower\": %d", mqttMessage, advertisedDevice.getTXPower());
    }
    sprintf(mqttMessage, "%s,\n  \"time\": \"%s\"", mqttMessage, timeString);
    sprintf(mqttMessage, "%s,\n  \"activeScan\": \"%s\"", mqttMessage, (heartbeat == 0 ? "yes" : "no"));
    sprintf(mqttMessage, "%s\n}", mqttMessage); // close json string
    sprintf(mqttTopic, "network/ble/%s/raw/%s", mqttClientID, bleAddress);
    client.publish(mqttTopic, mqttMessage);
    delay(100);
    if (advertisedDevice.haveServiceData()) {
      sprintf(mqttTopic, "network/ble/serviceUUID/%s/%s", advertisedDevice.getServiceDataUUID().toString().c_str(), bleAddress);
      client.publish(mqttTopic, mqttMessage);
      delay(100);
    }
  }
};

void networkReconnect() {
  while(WiFi.status() != WL_CONNECTED || !client.connected()) {
    Serial.printf(".. (re)connecting wifi ..");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.printf(".");
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(wifiSSID, wifiPass);
      delay(1000);
    }
    Serial.printf(" connected, IP Address: %s (MAC Address: %s)\n", WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
    Serial.printf(".. (re)setting NTP date/time.\n");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(1000);
    Serial.printf(".. (re)connecting mqtt ..");
    while(!client.connected()) {
      Serial.printf(".");
      client.setServer(mqttServer, 1883);
      sprintf(mqttTopic, "smartifier/%s/status", mqttClientID);
      client.connect(mqttClientID, mqttTopic, 1, 1, "0");
      delay(500);
      client.publish(mqttTopic, "1", true); // retain this message
      delay(500);
    }
    Serial.printf(" connected as %s.\n", mqttClientID);
  }
}

void setup(){
  Serial.begin(115200);
  Serial.printf("Smartifier - %s v%s: Initializing..", SMARTIFIERTYPE, SMARTIFIERVERSION);
  Serial.printf("Setup Bluetooth Low Energy (BLE).\n");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
  delay(500);
  sprintf(mqttClientID, "smartifier-%s", WiFi.macAddress().substring(9));
  
  networkReconnect();
  Serial.printf("Setup watchdog timer (%d seconds).\n", wdtTimeout);
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000 * 1000, false); //set time in us
  
  Serial.printf("Setup COMPLETE.\n");
}

void loop(){
  timerWrite(timer, 0); //reset timer (feed watchdog)

  if(!getLocalTime(&timeinfo)){
    Serial.printf("ERROR: Failed to obtain time\n");
    sprintf(timeString, "ERROR: Time");
  } else {
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  }

  // Reconnect network, if needed
  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    networkReconnect();
  }
  client.loop();

  if (boot) {
    strftime(bootTime, sizeof(bootTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
    boot = false;
  }
  
  // run BLE scan to find beacons. every 6th scan should be an active scan (others are passive)
  Serial.printf("%s, %s scan for BLE:\n", timeString, (heartbeat == 0 ? "Active" : "Passive"));
  pBLEScan->setActiveScan(heartbeat == 0); //active scan uses more power, but get results faster
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory

  if (heartbeat == 0) {
    Serial.printf("Update clock (via NTP).\n");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.printf("Update smartifier info topic.\n");
    sprintf(mqttTopic, "smartifier/%s/info", mqttClientID);
    sprintf(mqttMessage, "{\n  \"clientID\": \"%s\",\n  \"lastUpdate\": \"%s\",\n  \"ipAddress\": \"%s\",\n  \"macAddress\": \"%s\",\n  \"version\": \"%s\",\n  \"uptime\": %d,\n  \"bootTime\": \"%s\"\n}", mqttClientID, timeString, WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(), SMARTIFIERVERSION, millis() / 1000, bootTime);
    client.publish(mqttTopic, mqttMessage, true);
    heartbeat = 6;
  } else {
    heartbeat--;
  }
  delay(100);
}
