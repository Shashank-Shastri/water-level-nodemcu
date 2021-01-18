#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <SoftwareSerial.h>
#include "TFMini.h"
#include "Credentials.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
SoftwareSerial mySerial(D1, D2);      // Uno RX (TFMINI TX), Uno TX (TFMINI RX)
TFMini tfmini;
unsigned long previousMillisSensor = 0, previousMillis = 0; // access time
unsigned long currentMillis = 0; // will store last time firebase was updated
const long intervalSensor = 100; // interval at which to measure (milliseconds)
long interval = 60000*5;
float distance_m, lastDistance_m, distance_f, change_m, variation_m;
float actualHeight_m = 3.7, width_m = 4.5, length_m = 7.0, radius_m;
int level, volume, volumeC = 116550, distance_cm, strength;
String tankName = "Main Tank 2", currentPath = "/water-tank-info/current/M2", historicalPath = "/water-tank-info/historical/M2/";

//Define Firebase Data objects
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson waterLevel;
FirebaseJson timeStamp;

void otaSetup() {
    ArduinoOTA.setHostname(tankName.c_str());

    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void wifiSetup() {
    WiFi.mode(WIFI_STA);
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Connection Failed! Rebooting...");
        delay(5000);
    }
    Serial.println();
    Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void firebaseSetup() {
    config.host = FIREBASE_HOST;
    config.api_key = API_KEY;
    
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    
    Firebase.begin(&config, &auth);
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    mySerial.begin(TFMINI_BAUDRATE);
    tfmini.begin(&mySerial);
    wifiSetup();
    otaSetup();
    firebaseSetup();   
    timeClient.begin(); 
    timeClient.setTimeOffset(19800);
}

void updateReadings(String tankName, float distance, int volume, int level, bool overflowing, bool filling, String currentPath, String historicalPath) {
    timeStamp.set(".sv", "timestamp");
    waterLevel.set("distance", distance);
    waterLevel.set("volume", volume);
    waterLevel.set("timestamp", timeStamp);
    waterLevel.set("level", level);
    waterLevel.set("overflow", overflowing);
    waterLevel.set("filling", filling);
    waterLevel.set("tank", tankName);
    if (Firebase.set(firebaseData, currentPath, waterLevel) && Firebase.push(firebaseData, historicalPath, waterLevel)) Serial.println(firebaseData.jsonString());
    else Serial.println(firebaseData.errorReason());
}

float getVolume_Cylinder(float height_m, float radius_m) {
    return 3.14 * radius_m * radius_m * height_m * 1000;
}

float getVolume_Cuboid(float height_m, float length_m, float width_m) {
    return length_m * width_m * height_m * 1000;
}

float round(float var) { 
    // 37.66666 * 100 =3766.66 
    // 3766.66 + .5 =3767.16    for rounding off value 
    // then type cast to int so value is 3767 
    // then divided by 100 so the value converted into 37.67 
    float value = (int)(var * 100 + .5); 
    return (float)value / 100; 
}

void loop() {
    ArduinoOTA.handle();
    currentMillis = millis();
    if (currentMillis - previousMillisSensor >= intervalSensor) {
        previousMillisSensor = currentMillis; // save the last measure time
        // Take one TF Mini distance measurement
        distance_cm = tfmini.getDistance();
        distance_m = distance_cm * 0.01;
        distance_f = distance_m * 3.281;
        strength = tfmini.getRecentSignalStrength();
        change_m = distance_m - lastDistance_m;
        variation_m = abs(((float)change_m / (float)lastDistance_m) * 100);
        if(distance_m && distance_m < actualHeight_m && strength > 100 && (variation_m >= 15 || currentMillis - previousMillis >= interval)) {
            timeClient.update();
            unsigned long epochTime = timeClient.getEpochTime();
            struct tm *ptm = gmtime ((time_t *)&epochTime);
            int monthDay = ptm->tm_mday;
            int currentMonth = ptm->tm_mon+1;
            int currentYear = ptm->tm_year+1900;
            volume = round(getVolume_Cuboid(actualHeight_m - distance_m, length_m, width_m));
            level = round(((float)volume / (float)volumeC) * 100);
            lastDistance_m = distance_m;
            previousMillis = currentMillis;
            // Display the measurement
            Serial.print("Distance = ");
            Serial.print(distance_m);
            Serial.print("m | ");
            Serial.print(distance_f);
            Serial.print(" feet\tVolume = ");
            Serial.print(volume);
            Serial.print("L\tLevel = ");
            Serial.print(level);
            Serial.println("%");
            updateReadings(tankName, round(distance_f), volume, level, false, currentMillis - previousMillis < 60000, currentPath, historicalPath+currentYear+"/"+currentMonth+"/"+monthDay);
        }
    }
}
