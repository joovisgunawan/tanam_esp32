#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Arduino.h"
#include <sntp.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

#define printMutexCreationError(mutexName) Serial.printf(F("Failed to create mutex: %s\n"), mutexName)
#define printTaskCreationError(taskName) Serial.printf(F("Failed to create task: %s\n"), taskName)
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701
#define RXp2 16
#define TXp2 17
#define LED_PIN 13
#define DHT_PIN 15
#define TRIG_PIN 18
#define ECHO_PIN 19
#define DS18B20_PIN 4
#define WATER_FLOW_1_PIN 35
#define WATER_FLOW_2_PIN 34
#define RELAY_CH_0_PIN 2
#define RELAY_CH_1_PIN 13
#define RELAY_CH_2_PIN 14
#define RELAY_CH_3_PIN 27
#define RELAY_CH_4_PIN 26
#define RELAY_CH_5_PIN 25
#define RELAY_CH_6_PIN 33
#define RELAY_CH_7_PIN 32
#define RELAY_CH_8_PIN 23

struct NotificationParams {
  String title, body;
  // TaskHandle_t* taskHandle;
};

struct SaveDataParams {
  float airTempC, airTempF, humidity, waterTempC, waterTempF, ec, tds, pH, distanceCm, distanceInch;
};

namespace mqtt {
const char* mqtt_server = "mqtt.tanam.software";
const char* mqtt_broker = "tanam-broker";
const char* mqtt_password = "t4nAm_br0k3r";
const int mqtt_port = 1883;
const char* publish_to = "tanam1/publisher";
const char* subscribe_to = "tanam1/subscriber";
}

namespace ntp {
const char* ntpServer = "pool.ntp.org";
const int daylightOffset_sec = 28800;
const long gmtOffset_sec = 28800;
}

namespace sensor {
float airTempC, airTempF, humidity, waterTempC, waterTempF, ec, tds, pH = 0, distanceCm, distanceInch;
}

const char* client_id = "TANAM_1";
const char* serverName = "https://api.tanam.software:4430/data";
const int relayPins[] = {
  RELAY_CH_0_PIN,
  RELAY_CH_1_PIN,
  RELAY_CH_2_PIN,
  RELAY_CH_3_PIN,
  RELAY_CH_4_PIN,
  RELAY_CH_5_PIN,
  RELAY_CH_6_PIN,
  RELAY_CH_7_PIN,
  RELAY_CH_8_PIN,
};

bool isAutomate;

SemaphoreHandle_t relayMutex[9], airTempCMutex, airTempFMutex, humidityMutex, waterTempCMutex, waterTempFMutex, ecMutex, tdsMutex, pHMutex, distanceCmMutex, distanceInchMutex, httpMutex;
TaskHandle_t tdsNotification;
TaskHandle_t pHNotification;
TaskHandle_t airTempNotification;
TaskHandle_t waterTempNotification;
TaskHandle_t humidityNotification;
TaskHandle_t waterVolNotification;


WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT11);
OneWire oneWire(DS18B20_PIN);
DallasTemperature waterTempSensor(&oneWire);

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXp2, TXp2);
  pinMode(RELAY_CH_0_PIN, OUTPUT);
  pinMode(RELAY_CH_1_PIN, OUTPUT);
  pinMode(RELAY_CH_2_PIN, OUTPUT);
  pinMode(RELAY_CH_3_PIN, OUTPUT);
  pinMode(RELAY_CH_4_PIN, OUTPUT);
  pinMode(RELAY_CH_5_PIN, OUTPUT);
  pinMode(RELAY_CH_6_PIN, OUTPUT);
  pinMode(RELAY_CH_7_PIN, OUTPUT);
  pinMode(RELAY_CH_8_PIN, OUTPUT);
  pinMode(DHT_PIN, INPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 9; i++) {
    if ((relayMutex[i] = xSemaphoreCreateMutex()) == NULL) {
      char mutexName[20];
      snprintf(mutexName, sizeof(mutexName), "relayMutex[%d]", i);
      printMutexCreationError(mutexName);
    }
  }
  if ((airTempCMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("airTempCMutex");
  if ((airTempFMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("airTempFMutex");
  if ((humidityMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("humidityMutex");
  if ((waterTempCMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("waterTempCMutex");
  if ((waterTempFMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("waterTempFMutex");
  if ((ecMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("ecMutex");
  if ((tdsMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("tdsMutex");
  if ((pHMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("pHMutex");
  if ((distanceCmMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("distanceCmMutex");
  if ((distanceInchMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("distanceInchMutex");
  if ((httpMutex = xSemaphoreCreateMutex()) == NULL) printMutexCreationError("httpMutex");
  if (xTaskCreatePinnedToCore(initWifiManager, "initWifiManager task", 2048, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("initWifiManager");
  if (xTaskCreatePinnedToCore(initMQTT, "initMQTT task", 2048, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("initMQTT");
  if (xTaskCreatePinnedToCore(streamData, "streamData task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("streamData");
  if (xTaskCreatePinnedToCore(tdsMonitor, "tdsMonitor", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("tdsMonitor");
  if (xTaskCreatePinnedToCore(pHMonitor, "pHMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("pHMonitor");
  if (xTaskCreatePinnedToCore(airTemperatureMonitor, "airTemperatureMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("airTemperatureMonitor");
  if (xTaskCreatePinnedToCore(humidityMonitor, "airTemperatureMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("airTemperatureMonitor");
  if (xTaskCreatePinnedToCore(waterTemperatureMonitor, "waterTemperatureMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("waterTemperatureMonitor");
  if (xTaskCreatePinnedToCore(waterVolumeMonitor, "waterVolumeMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("waterVolumeMonitor");
  if (xTaskCreatePinnedToCore(simpanData, "waterVolumeMonitor task", 10000, NULL, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE) != pdPASS) printTaskCreationError("waterVolumeMonitor");
  if (xTaskCreate(arduino, "arduino task", 2048, NULL, 1, NULL) != pdPASS) printTaskCreationError("arduino");
  if (xTaskCreate(myLcd, "myLcd task", 4096, NULL, 1, NULL) != pdPASS) printTaskCreationError("myLcd");
  if (xTaskCreate(dht22, "dht22 task", 4096, NULL, 1, NULL) != pdPASS) printTaskCreationError("dht22");
  // if (xTaskCreate(tdsAutomation, "dht22 task", 4096, NULL, 1, NULL) != pdPASS) printTaskCreationError("dht22");

  configTime(ntp::gmtOffset_sec, ntp::daylightOffset_sec, ntp::ntpServer);
}

void loop() {
}

void initWifiManager(void* parameters) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  Serial.println(F(wm.autoConnect(client_id) ? "Connected to WiFi" : "Failed to connect to WiFi"));
  vTaskDelete(NULL);
}

void initMQTT(void* parameters) {
  client.setServer(mqtt::mqtt_server, mqtt::mqtt_port);
  client.setCallback(callback);
  vTaskDelete(NULL);
}

void callback(char* topic, byte* payload, unsigned int length) {
  char json[length + 1];
  for (int i = 0; i < length; i++) {
    json[i] = (char)payload[i];
  }
  json[length] = '\0';
  Serial.printf(F("Message arrived [%s] %s\n"), mqtt::subscribe_to, json);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf(F("deserializeJson() failed: %s\n"), error.c_str());
    return;
  }
  if (doc.containsKey("relay_ch")) {
    int relay_ch = doc["relay_ch"].as<int>();
    const char* state = doc["state"].as<const char*>();
    Serial.printf(F("relay_ch: %d\tstate: %s\n"), relay_ch, state);
    if (!(relay_ch >= 0 && relay_ch < 9)) {
      Serial.println(F("Invalid relay_ch value, it should be between 0-8"));
      return;
    }
    xSemaphoreTake(relayMutex[relay_ch], portMAX_DELAY);
    if (relay_ch != 0) {
      digitalWrite(relayPins[relay_ch], strcmp(state, "On") == 0 ? LOW : HIGH);
    } else {
      digitalWrite(relayPins[relay_ch], strcmp(state, "On") == 0 ? HIGH : LOW);
    }
    xSemaphoreGive(relayMutex[relay_ch]);
    Serial.printf(F("Relay Ch %d turned %s\n"), relay_ch, state);
  }
}

void reconnect() {
  Serial.print(F("Re-attempting MQTT connection..."));
  if (!client.connect(client_id, mqtt::mqtt_broker, mqtt::mqtt_password)) {
    Serial.printf(F("failed, rc=%d try again in 5 seconds\n"), client.state());
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    return;
  }
  Serial.println(F("Reconnected to mqtt broker"));
  client.subscribe(mqtt::subscribe_to);
}

void streamData(void* parameters) {
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("No connection, failed to stream data to mqtt, try again in 10 seconds"));
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }
    if (!client.connected()) {
      Serial.println(F("Not connected to mqtt broker, failed to stream data to mqtt"));
      reconnect();
      continue;
    }
    StaticJsonDocument<200> jsonDocument;
    xSemaphoreTake(airTempCMutex, portMAX_DELAY);
    jsonDocument["airTempC"] = String(sensor::airTempC, 2);
    xSemaphoreGive(airTempCMutex);

    xSemaphoreTake(airTempFMutex, portMAX_DELAY);
    jsonDocument["airTempF"] = String(sensor::airTempF, 2);
    xSemaphoreGive(airTempFMutex);

    xSemaphoreTake(humidityMutex, portMAX_DELAY);
    jsonDocument["humidity"] = String(sensor::humidity, 2);
    xSemaphoreGive(humidityMutex);

    xSemaphoreTake(waterTempCMutex, portMAX_DELAY);
    jsonDocument["waterTempC"] = String(sensor::waterTempC, 2);
    xSemaphoreGive(waterTempCMutex);

    xSemaphoreTake(waterTempFMutex, portMAX_DELAY);
    jsonDocument["waterTempF"] = String(sensor::waterTempF, 2);
    xSemaphoreGive(waterTempFMutex);

    xSemaphoreTake(ecMutex, portMAX_DELAY);
    jsonDocument["ec"] = String(sensor::ec, 2);
    xSemaphoreGive(ecMutex);

    xSemaphoreTake(tdsMutex, portMAX_DELAY);
    jsonDocument["tds"] = String(sensor::tds);
    xSemaphoreGive(tdsMutex);

    xSemaphoreTake(pHMutex, portMAX_DELAY);
    jsonDocument["pH"] = String(sensor::pH, 2);
    xSemaphoreGive(pHMutex);

    xSemaphoreTake(distanceCmMutex, portMAX_DELAY);
    jsonDocument["distanceCm"] = String(sensor::distanceCm, 2);
    xSemaphoreGive(distanceCmMutex);

    xSemaphoreTake(distanceInchMutex, portMAX_DELAY);
    jsonDocument["distanceInch"] = String(sensor::distanceInch, 2);
    xSemaphoreGive(distanceInchMutex);

    char buffer[200];
    serializeJson(jsonDocument, buffer);

    client.publish(mqtt::publish_to, buffer);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    client.loop();
  }
}




void arduino(void* parameters) {
  while (true) {
    if (Serial2.available()) {
      String receivedString = Serial2.readStringUntil('\n');
      Serial.printf(F("JSON Received from Arduino: %s\n"), receivedString.c_str());
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, receivedString);
      if (error) {
        Serial.printf(F("Failed to parse JSON from Arduino: %s\n"), error.c_str());
        continue;
      }
      if (doc.containsKey("phValue")) {
        xSemaphoreTake(pHMutex, portMAX_DELAY);
        sensor::pH = doc["phValue"];
        xSemaphoreGive(pHMutex);
      }
      if (doc.containsKey("ec")) {
        xSemaphoreTake(ecMutex, portMAX_DELAY);
        sensor::ec = doc["ec"];
        xSemaphoreGive(ecMutex);
      }
      if (doc.containsKey("tds")) {
        xSemaphoreTake(tdsMutex, portMAX_DELAY);
        sensor::tds = doc["tds"];
        xSemaphoreGive(tdsMutex);
      }
      if (doc.containsKey("waterTempC")) {
        xSemaphoreTake(waterTempCMutex, portMAX_DELAY);
        sensor::waterTempC = doc["waterTempC"];
        xSemaphoreGive(waterTempCMutex);
      }
      if (doc.containsKey("waterTempF")) {
        xSemaphoreTake(waterTempFMutex, portMAX_DELAY);
        sensor::waterTempF = doc["waterTempF"];
        xSemaphoreGive(waterTempFMutex);
      }
    }
  }
}




void tdsMonitor(void* pvParameters) {
  while (1) {
    xSemaphoreTake(tdsMutex, portMAX_DELAY);
    float tds = sensor::tds;
    xSemaphoreGive(tdsMutex);
    String title, body, tdsString = String(tds, 2);
    int relayState;
    if (tds > 20) {
      title = "TDS High";
      body = "TDS high (" + tdsString + "ppm)";
      relayState = LOW;
    } else if (tds < 5) {
      title = "TDS Low";
      body = "TDS low (" + tdsString + "ppm)";
      relayState = HIGH;

    } else {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    // for (int i = 0; i < 3; i++) {
    //   xSemaphoreTake(httpMutex, portMAX_DELAY);
    //   if (!sendNotification(title, body)) {
    //     xSemaphoreGive(httpMutex);
    //     vTaskDelay(5000 / portTICK_PERIOD_MS);
    //     continue;
    //   }
    //   break;
    // }

    // xSemaphoreGive(httpMutex);
    // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    // if (isAutomate) {
    //   xSemaphoreTake(relayMutex[0], portMAX_DELAY);
    //   xSemaphoreTake(relayMutex[1], portMAX_DELAY);
    //   digitalWrite(relayPins[0], relayState);
    //   digitalWrite(relayPins[1], relayState);
    //   xSemaphoreGive(relayMutex[0]);
    //   xSemaphoreGive(relayMutex[1]);
    // }

    NotificationParams* params = new NotificationParams{ title, body };
    xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, NULL);
    vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    // if (tdsNotification == NULL) {
    //   NotificationParams* params = new NotificationParams{ title, body, &tdsNotification };
    //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &tdsNotification);
    // } else {
    //   Serial.println("tdsNotification is exist");
    // }
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void tdsAutomation(void* pvParameters) {
  while (1) {
    if (!isAutomate) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    xSemaphoreTake(tdsMutex, portMAX_DELAY);
    float tds = sensor::tds;
    xSemaphoreGive(tdsMutex);

    //this will return 1(HIGH) 0(LOW) and -1
    int relayState = (tds > 20) ? LOW : ((tds < 5) ? HIGH : -1);

    if (relayState == -1) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    xSemaphoreTake(relayMutex[0], portMAX_DELAY);
    digitalWrite(relayPins[0], relayState);
    xSemaphoreGive(relayMutex[0]);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void pHMonitor(void* pvParameters) {
  while (1) {
    xSemaphoreTake(pHMutex, portMAX_DELAY);
    float pH = sensor::pH;
    xSemaphoreGive(pHMutex);
    String title, body, pHString = String(pH, 2);
    if (pH > 20) {
      title = "pH High";
      body = "pH high (" + pHString + ")";
    } else if (pH < 5) {
      title = "pH Low";
      body = "pH low (" + pHString + ")";
    } else {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    // for (int i = 0; i < 3; i++) {
    //   xSemaphoreTake(httpMutex, portMAX_DELAY);
    //   if (!sendNotification(title, body)) {
    //     xSemaphoreGive(httpMutex);
    //     vTaskDelay(5000 / portTICK_PERIOD_MS);
    //     continue;
    //   }
    //   break;
    // }

    // xSemaphoreGive(httpMutex);
    // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    NotificationParams* params = new NotificationParams{ title, body };
    xTaskCreate(sendNotificationCreator, "pHNotificationTask", 5000, params, 1, NULL);
    vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    // if (pHNotification == NULL) {
    //   NotificationParams* params = new NotificationParams{ title, body, &pHNotification };
    //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &pHNotification);
    // } else {
    //   Serial.println("pHNotification is exist");
    // }
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void humidityMonitor(void* pvParameters) {
  while (1) {
    xSemaphoreTake(humidityMutex, portMAX_DELAY);
    float humidity = sensor::pH;
    xSemaphoreGive(humidityMutex);
    String title, body, humidityString = String(humidity, 2);
    if (humidity > 20) {
      title = "Humidity Low";
      body = "Humidity high (" + humidityString + "%)";
    } else if (humidity < 5) {
      title = "Humidity Low";
      body = "Humidity low (" + humidityString + "%)";
    } else {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    // for (int i = 0; i < 3; i++) {
    //   xSemaphoreTake(httpMutex, portMAX_DELAY);
    //   if (!sendNotification(title, body)) {
    //     xSemaphoreGive(httpMutex);
    //     vTaskDelay(5000 / portTICK_PERIOD_MS);
    //     continue;
    //   }
    //   break;
    // }

    // xSemaphoreGive(httpMutex);
    // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    NotificationParams* params = new NotificationParams{ title, body };
    xTaskCreate(sendNotificationCreator, "pHNotificationTask", 5000, params, 1, NULL);
    vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    // if (humidityNotification == NULL) {
    //   NotificationParams* params = new NotificationParams{ title, body, &humidityNotification };
    //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &humidityNotification);
    // } else {
    //   Serial.println("humidityNotification is exist");
    // }
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void airTemperatureMonitor(void* pvParameters) {
  (void)pvParameters;
  while (1) {
    if (xSemaphoreTake(airTempCMutex, portMAX_DELAY) == pdTRUE) {
      float airTempC = sensor::pH;
      xSemaphoreGive(airTempCMutex);
      String title, body, airTempCString = String(airTempC, 2);
      if (airTempC > 20) {
        title = "Air Temperature High";
        body = "Air temperature high (" + airTempCString + "°C)";
      } else if (airTempC < 5) {
        title = "Air Temperature Low";
        body = "Air temperature low (" + airTempCString + "°C)";
      } else {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        continue;
      }
      // for (int i = 0; i < 3; i++) {
      //   xSemaphoreTake(httpMutex, portMAX_DELAY);
      //   if (!sendNotification(title, body)) {
      //     xSemaphoreGive(httpMutex);
      //     vTaskDelay(5000 / portTICK_PERIOD_MS);
      //     continue;
      //   }
      //   break;
      // }

      // xSemaphoreGive(httpMutex);
      // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      NotificationParams* params = new NotificationParams{ title, body };
      xTaskCreate(sendNotificationCreator, "pHNotificationTask", 5000, params, 1, NULL);
      vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      // if (airTempNotification == NULL) {
      //   NotificationParams* params = new NotificationParams{ title, body, &airTempNotification };
      //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &airTempNotification);
      // } else {
      //   Serial.println("airTempNotification is exist");
      // }
      // vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}

void waterTemperatureMonitor(void* pvParameters) {
  (void)pvParameters;
  while (1) {
    if (xSemaphoreTake(waterTempCMutex, portMAX_DELAY) == pdTRUE) {
      float waterTempC = sensor::waterTempC;
      xSemaphoreGive(waterTempCMutex);
      String title, body, waterTempCString = String(waterTempC, 2);
      if (waterTempC > 20) {
        title = "Water Temperature High";
        body = "Water temperature high (" + waterTempCString + "°C)";
      } else if (waterTempC < 5) {
        title = "Water Temperature Low";
        body = "Water temperature low (" + waterTempCString + "°C)";
      } else {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        continue;
      }
      // for (int i = 0; i < 3; i++) {
      //   xSemaphoreTake(httpMutex, portMAX_DELAY);
      //   if (!sendNotification(title, body)) {
      //     xSemaphoreGive(httpMutex);
      //     vTaskDelay(5000 / portTICK_PERIOD_MS);
      //     continue;
      //   }
      //   break;
      // }

      // xSemaphoreGive(httpMutex);
      // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      NotificationParams* params = new NotificationParams{ title, body };
      xTaskCreate(sendNotificationCreator, "waterTemperatureNotificationTask", 5000, params, 1, NULL);
      vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      // if (waterTempNotification == NULL) {
      //   NotificationParams* params = new NotificationParams{ title, body, &waterTempNotification };
      //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &waterTempNotification);
      // } else {
      //   Serial.println("waterTempNotification is exist");
      // }
      // vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}

void waterVolumeMonitor(void* pvParameters) {
  (void)pvParameters;
  while (1) {
    if (xSemaphoreTake(distanceCmMutex, portMAX_DELAY) == pdTRUE) {
      float distanceCm = sensor::distanceCm;
      xSemaphoreGive(distanceCmMutex);
      String title, body;
      if (distanceCm > 20) {
        title = "Water Volume Low";
        body = "Water volume low";
      } else if (distanceCm < 5) {
        title = "Water Volume High";
        body = "Water volume hign ";
      } else {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        continue;
      }
      // for (int i = 0; i < 3; i++) {
      //   xSemaphoreTake(httpMutex, portMAX_DELAY);
      //   if (!sendNotification(title, body)) {
      //     xSemaphoreGive(httpMutex);
      //     vTaskDelay(5000 / portTICK_PERIOD_MS);
      //     continue;
      //   }
      //   break;
      // }

      // xSemaphoreGive(httpMutex);
      // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      NotificationParams* params = new NotificationParams{ title, body };
      xTaskCreate(sendNotificationCreator, "waterVolumeNotificationTask", 5000, params, 1, NULL);
      vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

      // if (waterVolNotification == NULL) {
      //   NotificationParams* params = new NotificationParams{ title, body, &waterVolNotification };
      //   xTaskCreate(sendNotificationCreator, "tdsNotificationTask", 5000, params, 1, &waterVolNotification);
      // } else {
      //   Serial.println("waterVolNotification is exist");
      // }
      // vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}

void sendNotificationCreator(void* pvParameters) {
  NotificationParams* params = static_cast<NotificationParams*>(pvParameters);
  for (int i = 0; i < 3; i++) {
    xSemaphoreTake(httpMutex, portMAX_DELAY);
    if (sendNotification(params->title, params->body)) {
      xSemaphoreGive(httpMutex);
      break;
    }
    xSemaphoreGive(httpMutex);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
  // Serial.println("task is paused");
  // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);
  // Serial.println("task handler is deleted");
  // *(params->taskHandle) = NULL;
  delete params;
  // Serial.println("task is deleted");

  vTaskDelete(NULL);
}

bool sendNotification(const String& title, const String& body) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf(F("No connection, failed to notify %s, try again in 10 seconds\n"), title.c_str());
    vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
    return false;
  }
  StaticJsonDocument<200> jsonDocument;
  jsonDocument["title"] = title;
  jsonDocument["body"] = body;
  jsonDocument["deviceId"] = client_id;
  char buffer[200];
  serializeJson(jsonDocument, buffer);
  HTTPClient http;
  http.begin("https://api.tanam.software:4430/notif");
  http.addHeader("Content-Type", "application/json");
  int httpStatusCode = http.POST(buffer);
  String response = http.getString();
  http.end();
  if (httpStatusCode != 200) {
    Serial.printf(F("Failed to send %s notification. HTTP status code: %d, try again in 5 seconds\n"), title.c_str(), httpStatusCode);
    return false;
  }

  Serial.printf(F("%s notification sent successfully:\n%s\n"), title.c_str(), response.c_str());
  return true;
}

void dht22(void* parameters) {
  Serial.println(F("dht22 task started"));
  for (;;) {
    xSemaphoreTake(airTempCMutex, portMAX_DELAY);
    xSemaphoreTake(airTempFMutex, portMAX_DELAY);
    sensor::airTempC = dht.readTemperature();
    sensor::airTempF = dht.convertCtoF(sensor::airTempC);
    if (isnan(sensor::airTempC)) {
      sensor::airTempC = 0.0;
    }
    if (isnan(sensor::airTempF)) {
      sensor::airTempF = 0.0;
    }
    xSemaphoreGive(airTempCMutex);
    xSemaphoreGive(airTempFMutex);

    xSemaphoreTake(humidityMutex, portMAX_DELAY);
    sensor::humidity = dht.readHumidity();
    if (isnan(sensor::humidity)) {
      sensor::humidity = 0.0;
    }
    xSemaphoreGive(humidityMutex);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void myLcd(void* parameters) {
  lcd.init();
  lcd.backlight();
  displayLoading();
  for (;;) {
    displayTime();
    displayDHT22();
    displayDs18b20();
    displayTds();
    displaypH();
  }
}

void displayLoading() {
  lcd.clear();
  lcd.print("Welcome to Tanam");
  String dot = "";
  int totalDot = 0;
  for (int i = 0; i < 7; i++) {
    if (totalDot < 3) {
      dot += ".";
      totalDot++;
    } else {
      dot = "";
      totalDot = 0;
    }
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Loading");
    lcd.print(dot);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void displayTime() {
  lcd.clear();
  for (int i = 0; i < 5; i++) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      lcd.print("Time is not");
      lcd.setCursor(0, 1);
      lcd.print("available");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      return;
    }
    lcd.setCursor(0, 0);
    lcd.print(&timeinfo, "%a, %d-%b-%Y");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(&timeinfo, "%H:%M:%S");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void displayDHT22() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(sensor::airTempC);
  lcd.print("\xDF"
            "C");
  lcd.setCursor(0, 1);
  lcd.print("Humid: ");
  lcd.print(sensor::humidity);
  lcd.print("%");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void displayDs18b20() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("W Temp: ");
  lcd.print(sensor::waterTempC);
  lcd.print("\xDF"
            "C");
  lcd.setCursor(0, 1);
  lcd.print("W Temp: ");
  lcd.print(sensor::waterTempF);
  lcd.print("\xDF"
            "F");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void displayTds() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("EC: ");
  lcd.print(sensor::ec);
  lcd.setCursor(0, 1);
  lcd.print("TDS: ");
  lcd.print(sensor::tds);
  lcd.print("PPM");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void displaypH() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("EC: ");
  lcd.print(sensor::ec);
  lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("pH: ");
  lcd.print(sensor::pH);
  lcd.print("%");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}

bool saveData(const float& airTempC, const float& airTempF, const float& humidity, const float& waterTempC, const float& waterTempF, const float& ec, const float& tds, const float& pH, const float& distanceCm, const float& distanceInch) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf(F("No connection, failed to save data, try again in 10 seconds\n"));
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    return false;
  }

  StaticJsonDocument<200> jsonDocument;
  jsonDocument["airTempC"] = airTempC;
  jsonDocument["airTempF"] = airTempF;
  jsonDocument["humidity"] = humidity;
  jsonDocument["waterTempC"] = waterTempC;
  jsonDocument["waterTempF"] = waterTempF;
  jsonDocument["ec"] = ec;
  jsonDocument["tds"] = tds;
  jsonDocument["pH"] = pH;
  jsonDocument["distanceCm"] = distanceCm;
  jsonDocument["distanceInch"] = distanceInch;
  char buffer[200];
  serializeJson(jsonDocument, buffer);
  HTTPClient http;
  http.begin("https://api.tanam.software:4430/data");
  http.addHeader("Content-Type", "application/json");
  int httpStatusCode = http.POST(buffer);
  String response = http.getString();
  http.end();

  if (httpStatusCode != 200) {
    Serial.printf(F("Failed to save data. HTTP status code: %d, try again in 5 seconds\n"), httpStatusCode);
    return false;
  }

  Serial.printf(F("Data is saved successfully:\n%s\n"), response.c_str());
  return true;
}

void simpanData(void* parameters) {
  Serial.println(F("Stream data task started"));
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("No connection, failed to stream data to mqtt, try again in 10 seconds"));
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }
    float airTempC, airTempF, humidity, waterTempC, waterTempF, ec, tds, pH, distanceCm, distanceInch;

    // xSemaphoreTake(httpMutex, portMAX_DELAY);
    // StaticJsonDocument<200> jsonDocument;
    // xSemaphoreTake(airTempCMutex, portMAX_DELAY);
    // jsonDocument["airTempC"] = String(sensor::airTempC, 2);
    // xSemaphoreGive(airTempCMutex);

    // xSemaphoreTake(airTempFMutex, portMAX_DELAY);
    // jsonDocument["airTempF"] = String(sensor::airTempF, 2);
    // xSemaphoreGive(airTempFMutex);

    // xSemaphoreTake(humidityMutex, portMAX_DELAY);
    // jsonDocument["humidity"] = String(sensor::humidity, 2);
    // xSemaphoreGive(humidityMutex);

    // xSemaphoreTake(waterTempCMutex, portMAX_DELAY);
    // jsonDocument["waterTempC"] = String(sensor::waterTempC, 2);
    // xSemaphoreGive(waterTempCMutex);

    // xSemaphoreTake(waterTempFMutex, portMAX_DELAY);
    // jsonDocument["waterTempF"] = String(sensor::waterTempF, 2);
    // xSemaphoreGive(waterTempFMutex);

    // xSemaphoreTake(ecMutex, portMAX_DELAY);
    // jsonDocument["ec"] = String(sensor::ec, 2);
    // xSemaphoreGive(ecMutex);

    // xSemaphoreTake(tdsMutex, portMAX_DELAY);
    // jsonDocument["tds"] = String(sensor::tds);
    // xSemaphoreGive(tdsMutex);

    // xSemaphoreTake(pHMutex, portMAX_DELAY);
    // jsonDocument["pH"] = String(sensor::pH, 2);
    // xSemaphoreGive(pHMutex);

    // xSemaphoreTake(distanceCmMutex, portMAX_DELAY);
    // jsonDocument["distanceCm"] = String(sensor::distanceCm, 2);
    // xSemaphoreGive(distanceCmMutex);

    // xSemaphoreTake(distanceInchMutex, portMAX_DELAY);
    // jsonDocument["distanceInch"] = String(sensor::distanceInch, 2);
    // xSemaphoreGive(distanceInchMutex);

    xSemaphoreTake(airTempCMutex, portMAX_DELAY);
    airTempC = sensor::airTempC;
    xSemaphoreGive(airTempCMutex);

    xSemaphoreTake(airTempFMutex, portMAX_DELAY);
    airTempF = sensor::airTempF;
    xSemaphoreGive(airTempFMutex);

    xSemaphoreTake(humidityMutex, portMAX_DELAY);
    humidity = sensor::humidity;
    xSemaphoreGive(humidityMutex);

    xSemaphoreTake(waterTempCMutex, portMAX_DELAY);
    waterTempC = sensor::waterTempC;
    xSemaphoreGive(waterTempCMutex);

    xSemaphoreTake(waterTempFMutex, portMAX_DELAY);
    waterTempF = sensor::waterTempF;
    xSemaphoreGive(waterTempFMutex);

    xSemaphoreTake(ecMutex, portMAX_DELAY);
    ec = sensor::ec;
    xSemaphoreGive(ecMutex);

    xSemaphoreTake(tdsMutex, portMAX_DELAY);
    tds = sensor::tds;
    xSemaphoreGive(tdsMutex);

    xSemaphoreTake(pHMutex, portMAX_DELAY);
    pH = sensor::pH;
    xSemaphoreGive(pHMutex);

    xSemaphoreTake(distanceCmMutex, portMAX_DELAY);
    distanceCm = sensor::distanceCm;
    xSemaphoreGive(distanceCmMutex);

    xSemaphoreTake(distanceInchMutex, portMAX_DELAY);
    distanceInch = sensor::distanceInch;
    xSemaphoreGive(distanceInchMutex);

    // for (int i = 0; i < 3; i++) {
    //   xSemaphoreTake(httpMutex, portMAX_DELAY);
    //   if (!saveData(airTempC, airTempF, humidity, waterTempC, waterTempF, ec, tds, pH, distanceCm, distanceInch)) {
    //     xSemaphoreGive(httpMutex);
    //     vTaskDelay(5000 / portTICK_PERIOD_MS);
    //     continue;
    //   }
    //   break;
    // }

    // xSemaphoreGive(httpMutex);
    // vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);

    SaveDataParams* params = new SaveDataParams{ airTempC, airTempF, humidity, waterTempC, waterTempF, ec, tds, pH, distanceCm, distanceInch };
    xTaskCreate(saveDataCreator, "saveDataTask", 10000, params, 1, NULL);
    vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);
  }
}

void saveDataCreator(void* pvParameters) {
  SaveDataParams* params = static_cast<SaveDataParams*>(pvParameters);
  for (int i = 0; i < 3; i++) {
    xSemaphoreTake(httpMutex, portMAX_DELAY);
    if (saveData(params->airTempC, params->airTempF, params->humidity, params->waterTempC, params->waterTempF, params->ec, params->tds, params->pH, params->distanceCm, params->distanceInch)) {
      xSemaphoreGive(httpMutex);
      break;
    }
    xSemaphoreGive(httpMutex);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
  delete params;  // Clean up dynamically allocated memory
  vTaskDelete(NULL);
}
