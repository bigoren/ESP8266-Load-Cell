// Required Libarys:
// async-mqtt-client:  https://codeload.github.com/marvinroger/async-mqtt-client/zip/master
// ESPAsyncTCP:        https://codeload.github.com/me-no-dev/ESPAsyncTCP/zip/master
// HX711:              https://codeload.github.com/bogde/HX711/zip/master

#include "secrets.h"

// MQTT Broker Config
#define MQTT_HOST IPAddress(10, 0, 0, 200)
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#define MQTT_RECONNECT_TIME 2 // in seconds

// MQTT Client Config
#define MQTT_CLIENT_ID "sensors/bama"
#define MQTT_TOPIC_LOAD MQTT_CLIENT_ID "/load"
#define MQTT_TOPIC_LOAD_QoS 0 // Keep 0 if you don't know what it is doing
#define MQTT_TOPIC_TARE MQTT_CLIENT_ID "/tare"
#define MQTT_TOPIC_TARE_QoS 0 // Keep 0 if you don't know what it is doing
#define MQTT_TOPIC_LEDS MQTT_CLIENT_ID "/leds"
#define MQTT_TOPIC_LEDS_QoS 0 // Keep 0 if you don't know what it is doing

// WiFi Config
#define WIFI_CLIENT_ID "BAMA"
#define WIFI_RECONNECT_TIME 2 // in seconds

// Wifi optional static ip (leave client ip empty to disable)
#define WIFI_STATIC_IP    0 // set to 1 to enable static ip
#define WIFI_CLIENT_IP    IPAddress(10, 0, 0, 210)
#define WIFI_GATEWAY_IP   IPAddress(10, 0, 0, 1)
#define WIFI_SUBNET_IP    IPAddress(255, 255, 255, 0)
#define WIFI_DNS_IP       IPAddress(10, 0, 0, 1)

#define OTA_PATCH 1 // Set to 0 if you don't want to update your code over-the-air
#define OTA_PASS "set_ota_password"

// HX711 Config scale 1 pins
#define HX711_PIN_DOUT D2
#define HX711_PIN_CLK  D3
// HX711 Config scale 2 pins
#define HX711_PIN_DOUT2 D5
#define HX711_PIN_CLK2  D6

// Get Data:
#define CALIBRATION   -11000 // ADC bits to Kg conversion factor | modify this value if your reading are way off. Different cable length result in different values.
#define SAMPLE_PERIOD 5     // sample period in ms
#define NUM_SAMPLES   5      // number of samples - max 255 - published mqtt update every NUM_SAMPLES * SAMPLE_PERIOD = ms

// Send Data:
#define RESOLUTION    0      // number of characters after the decimal - 0, 1 or 2 is supported.

// Save the Tare to EEPROM.
// Set 0 to disable. It'll tare once the device has started.
// Set 1 to save the tare offset to EEPROM - it'll be saved while restarting/power off
#define SAVE_TARE      1
#define EEPROM_ADDRESS 0 // You can change this adress after a while to reduce the wear on the EEPROM

// FastLED config
#if FASTLED_VERSION < 3001000
#error "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define DATA_PIN    9
//#define CLK_PIN   4
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
#define NUM_LEDS    250
#define BRIGHTNESS  100
#define OUTER_RING_NUM 130
#define INNER_RING_NUM 120
#define INNER_RING_START OUTER_RING_NUM
