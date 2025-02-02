// Required Libarys:
// async-mqtt-client:  https://codeload.github.com/marvinroger/async-mqtt-client/zip/master
// ESPAsyncTCP:        https://codeload.github.com/me-no-dev/ESPAsyncTCP/zip/master
// HX711:              https://codeload.github.com/bogde/HX711/zip/master
#define ARDUINOJSON_USE_LONG_LONG 1
#define FASTLED_ESP8266_DMA

#include <FastLED.h>
#include <ArduinoJson.h>
//#include "Arduino.h" // for millis() function
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <HX711.h>
#include <ArduinoOTA.h>
#include <stdlib.h>
#include "config.h"

#define FILTER_SIZE 1

enum AnimationMode {
  AnimationModeClear = 0,
  AnimationModeConfetti = 1,
  AnimationModeFill = 2,
  AnimationModeMovingSegments = 3,
  AnimationModeRainbow = 4,
};


CRGB leds[NUM_LEDS];
uint8_t gHue = 0; // rotating "base color" used by many of the patterns
float fillPercent = 0.0;
AnimationMode animationMode = AnimationModeClear;

HX711 scale;
HX711 scale2;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

bool firstRun = true;
unsigned char samples;
int sum, median, offset;
float weight;
char oldResult[10];

void eeWriteInt(int pos, int val) {
    byte* p = (byte*) &val;
    EEPROM.write(pos, *p);
    EEPROM.write(pos + 1, *(p + 1));
    EEPROM.write(pos + 2, *(p + 2));
    EEPROM.write(pos + 3, *(p + 3));
    EEPROM.commit();
}

int eeGetInt(int pos) {
  int val;
  byte* p = (byte*) &val;
  *p        = EEPROM.read(pos);
  *(p + 1)  = EEPROM.read(pos + 1);
  *(p + 2)  = EEPROM.read(pos + 2);
  *(p + 3)  = EEPROM.read(pos + 3);
  return val;
}

void connectToMqtt() {
  Serial.println("[MQTT] Connecting to MQTT...");
  mqttClient.setClientId("LOAD_CELLS");
  mqttClient.setKeepAlive(5);
  mqttClient.setWill(MQTT_TOPIC_LOAD,MQTT_TOPIC_LOAD_QoS,true,"{}",2);
  mqttClient.connect();
}

void connectToWifi() {
  Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);

  WiFi.hostname(WIFI_CLIENT_ID);
  WiFi.mode(WIFI_STA);
  if (WIFI_STATIC_IP != 0) {
    WiFi.config(WIFI_CLIENT_IP, WIFI_GATEWAY_IP, WIFI_SUBNET_IP, WIFI_DNS_IP);
  }

  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  delay(10000);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("[WiFi] timeout, disconnecting WiFi");
    ESP.restart();
  }
  else {
    Serial.println(WiFi.localIP());
    wifiReconnectTimer.detach();
    connectToMqtt();
  }
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.print("[WiFi] Connected, IP address: ");
  Serial.println(WiFi.localIP());
  wifiReconnectTimer.detach();
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("[WiFi] Disconnected from Wi-Fi!");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  //wifiReconnectTimer.once(WIFI_RECONNECT_TIME, connectToWifi);
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("[MQTT] Connected to MQTT!");

  mqttReconnectTimer.detach();

  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  uint16_t packetIdSub = mqttClient.subscribe(MQTT_TOPIC_TARE, MQTT_TOPIC_TARE_QoS);
  Serial.print("Subscribing to ");
  Serial.println(MQTT_TOPIC_TARE);
  uint16_t packetIdSub2 = mqttClient.subscribe(MQTT_TOPIC_LEDS, MQTT_TOPIC_LEDS_QoS);
  Serial.print("Subscribing to ");
  Serial.println(MQTT_TOPIC_LEDS);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("[MQTT] Disconnected from MQTT!");

  if (WiFi.isConnected()) {
    Serial.println("[MQTT] Trying to reconnect...");
    mqttReconnectTimer.once(MQTT_RECONNECT_TIME, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (!strcmp(topic, MQTT_TOPIC_TARE)) {
    Serial.print("Zeroing: ");
    Serial.println(median / (float) 100);
    offset = median;
    if (SAVE_TARE != 0) {
      Serial.print("Saving to EEPROM...");
      eeWriteInt(EEPROM_ADDRESS, offset);
    }
  }
  if (!strcmp(topic,MQTT_TOPIC_LEDS)) {
    Serial.print("Leds message: ");
    Serial.print(len);
    Serial.print(" [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < len; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    fillPercent = doc["led_percent"];
    gHue = doc["led_color"];
    animationMode = (AnimationMode)doc["animation_mode"].as<int>();
    Serial.print("led_percent: ");
    Serial.println(fillPercent);
    Serial.print("led color: ");
    Serial.println(gHue);
    FastLED.clear();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Serial started");
  EEPROM.begin(512);
  Serial.println("EEPROM started");
  // NOTE: scale begins are blocking! if no HX711 is connected ESP will freeze
  scale.begin(HX711_PIN_DOUT, HX711_PIN_CLK);
  scale2.begin(HX711_PIN_DOUT2, HX711_PIN_CLK2);
  Serial.println("HX711 begin ended!");

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip)
         .setDither(BRIGHTNESS < 255);
  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(D0, OUTPUT);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  if (MQTT_USER != "") {
    mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
  }

  if (SAVE_TARE == 1) {
    offset = eeGetInt(EEPROM_ADDRESS);

    Serial.print("Loading offset from EEPROM: ");
    Serial.println(offset / (float) 100);
  }

  scale.set_scale(CALIBRATION);
  scale2.set_scale(CALIBRATION);

  if (OTA_PATCH == 1) {
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

ArduinoOTA.setHostname(WIFI_CLIENT_ID);
  //ArduinoOTA.setPassword(OTA_PASS);

    ArduinoOTA.begin();
  }

  connectToWifi();
  // Turn OFF board led after wifi connect
  digitalWrite(D0, LOW);
}

int compare(const void* a, const void* b)
{
     int int_a = * ( (int*) a );
     int int_b = * ( (int*) b );

     if ( int_a == int_b ) return 0;
     else if ( int_a < int_b ) return -1;
     else return 1;
}

long prevTime, currTime=0;
long prevStamp, stamp=0;

void loop() {
  if (WiFi.isConnected() && (OTA_PATCH != 0)) {
    ArduinoOTA.handle();
  }

  prevTime = currTime;
  currTime = millis();
  if ((currTime - prevTime) > SAMPLE_PERIOD) {
    // static filter variables
    static int filterSamples[FILTER_SIZE];
    static int filterHead = 0;

    // take sample and increment head
    filterSamples[filterHead] = ((scale2.get_units() * 100) - (scale.get_units() * 100)) / 2;
    filterHead = (filterHead + 1) % FILTER_SIZE;

    // copy filter array
    int sortedSamples[FILTER_SIZE];
    for (int i = 0; i < FILTER_SIZE; i++)
    {
      sortedSamples[i] = filterSamples[i];
    }

    // sort the array copy
    qsort(sortedSamples, FILTER_SIZE, sizeof(int), compare);
    median = sortedSamples[FILTER_SIZE / 2];

    if (firstRun) {
      firstRun = false;
      if (SAVE_TARE == 0) {
        median = offset;
        return;
      }
    }

    weight = ((median-offset) / (float) 100);
    char result[10];
    dtostrf(((median-offset) / (float) 100), 5, RESOLUTION, result);
    if (mqttClient.connected() && strcmp(result, oldResult)) {
      prevStamp = stamp;
      stamp = millis();
      StaticJsonDocument<128> json_doc;
      json_doc["weight"] = result;
      json_doc["timeDiff"] = (stamp - prevStamp); 
      char json_result[40];
      serializeJson(json_doc, json_result);
      mqttClient.publish(MQTT_TOPIC_LOAD, MQTT_TOPIC_LOAD_QoS, true, json_result);
      serializeJson(json_doc, Serial);
      Serial.println();
    }

    strncpy(oldResult, result, 10);
    // if (weight < 2.0) {
    //   animationMode = AnimationModeMovingSegments;
    // }
    // if ((weight > 2.0) & (weight <= 70.0)) {
    //   fillPercent = 200 / weight;
    //   animationMode = AnimationModeFill;
    // }
    // if (weight > 70.0) {
    //   animationMode = AnimationModeRainbow;
    // }
  }

  switch(animationMode) {

    case AnimationModeConfetti:
    {
      fadeToBlackBy( leds, NUM_LEDS, 13);
      for(int i=0; i<5; i++) {
        int pos = random16(NUM_LEDS);
        leds[pos] += CHSV( beat8(5), 200, 255);
      }
    }
    break;

    case AnimationModeFill:
    {
      FastLED.clear();
      int endIndexOuter = (int)(fillPercent * OUTER_RING_NUM);
      for (int i = 0; i < endIndexOuter; i++)
      {
        leds[i] = CHSV(gHue, 255, 255);
      }

      int endIndexInner = (int)(fillPercent * INNER_RING_NUM);
      for (int i = 0; i < endIndexInner; i++)
      {
        leds[i + INNER_RING_START] = CHSV(gHue, 255, 255);
      }
    }
    break;

    case AnimationModeMovingSegments: 
    {
      float timeOffset = beat8(20) / 255.0;
      uint8_t brightness = beatsin8(30);

      FastLED.clear();
      for(int i=0; i<5; i++) {
        float relStart = i * 0.2 + timeOffset;
        float relEnd = relStart + 0.08;

        uint8_t hue = gHue;

        int startIndexOuter = (int)(relStart * OUTER_RING_NUM);
        int endIndexOuter = (int)(relEnd * OUTER_RING_NUM);
        for (int j = startIndexOuter; j < endIndexOuter; j++)
        {
          int ledIndex = j % OUTER_RING_NUM;
          leds[ledIndex] = CHSV(hue, 255, brightness);
        }

        int startIndexInner = (int)(relStart * INNER_RING_NUM);
        int endIndexInner = (int)(relEnd * INNER_RING_NUM);
        for (int j = startIndexInner; j < endIndexInner; j++)
        {
          int ledIndex = j % INNER_RING_NUM + INNER_RING_START;
          leds[ledIndex] = CHSV(hue, 255, brightness);
        }
        
      }
    }
    break;

    case AnimationModeRainbow: 
    {
        uint8_t hueShift = beat8(30);
        for (int i = 0; i < OUTER_RING_NUM; i++)
        {
          float rel = (float)i / (float)OUTER_RING_NUM;
          leds[i] = CHSV(hueShift + (uint8_t)(rel * 255), 255, 255);
        }      

        for (int i = 0; i < INNER_RING_NUM; i++)
        {
          float rel = (float)i / (float)INNER_RING_NUM;
          leds[i + INNER_RING_START] = CHSV(hueShift + (uint8_t)(rel * 255), 255, 255);
        }      
    }
    break;

    default:
      FastLED.clear();

  }

  FastLED.show();
  // gHue++;
  // delay(SAMPLE_PERIOD);
}
