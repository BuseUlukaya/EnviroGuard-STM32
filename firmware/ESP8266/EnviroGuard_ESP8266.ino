#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <stdio.h>
#include <string.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

constexpr uint16_t TCP_PORT = 8080;
constexpr int STM32_RX_PIN = 14; // NodeMCU D5 <- STM32 PA2
constexpr uint32_t STM32_BAUD = 115200;

SoftwareSerial stm32Serial;
WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

char lineBuffer[160];
size_t lineLength = 0;
bool discardLine = false;
bool serverRunning = false;
uint32_t lastWifiReport = 0;
float lastTemperature = 0;
float lastHumidity = 0;
bool measurementValid = false;
uint32_t lastMeasurementTime = 0;
bool liveStream = false;
char commandBuffer[32];
size_t commandLength = 0;
bool discardCommand = false;

void processMeasurement(const char* line) {
  float temperature, humidity;
  int consumed = 0;
  if (sscanf(line, "Temperature: %f C | Humidity: %f %% %n",
             &temperature, &humidity, &consumed) == 2 &&
      consumed > 0 && line[consumed] == '\0' &&
      temperature >= -40 && temperature <= 80 &&
      humidity >= 0 && humidity <= 100) {
    lastTemperature = temperature;
    lastHumidity = humidity;
    lastMeasurementTime = millis();
    measurementValid = true;
  } else if (strncmp(line, "DHT22", 5) == 0) {
    measurementValid = false;
  }
}

void handleCommand(const char* command) {
  if (strcmp(command, "CANLI") == 0) {
    liveStream = true;
    tcpClient.println("CANLI ON");
    return;
  }
  if (strcmp(command, "DUR") == 0) {
    liveStream = false;
    tcpClient.println("CANLI OFF");
    return;
  }
  bool temperatureRequested = strcmp(command, "SICAKLIK") == 0;
  bool humidityRequested = strcmp(command, "NEM") == 0;
  bool bothRequested = strcmp(command, "VERI") == 0;
  if (!temperatureRequested && !humidityRequested && !bothRequested) {
    tcpClient.println("ERROR: SICAKLIK / NEM / VERI / CANLI / DUR");
    return;
  }
  if (!measurementValid || millis() - lastMeasurementTime > 10000) {
    tcpClient.println("ERROR: Guncel STM32 olcumu yok");
    return;
  }
  if (temperatureRequested || bothRequested) {
    tcpClient.print("Temperature: ");
    tcpClient.print(lastTemperature, 1);
    tcpClient.print(" C");
  }
  if (bothRequested) tcpClient.print(" | ");
  if (humidityRequested || bothRequested) {
    tcpClient.print("Humidity: ");
    tcpClient.print(lastHumidity, 1);
    tcpClient.print(" %");
  }
  tcpClient.println();
}

void readCommands() {
  for (int n = 0; n < 128 && tcpClient.available(); ++n) {
    char c = static_cast<char>(tcpClient.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (discardCommand) {
        tcpClient.println("ERROR: Gecersiz/uzun komut");
      } else if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
      }
      commandLength = 0;
      discardCommand = false;
    } else if (!discardCommand) {
      if (c < 32 || c > 126 || commandLength >= sizeof(commandBuffer) - 1) {
        discardCommand = true;
        commandLength = 0;
      } else {
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
        commandBuffer[commandLength++] = c;
      }
    }
  }
}

void readSTM32() {
  if (stm32Serial.overflow()) {
    lineLength = 0;
    discardLine = true;
    Serial.println("UART tampon tasmasi: satir atlandi.");
  }

  while (stm32Serial.available()) {
    int value = stm32Serial.read();
    if (value < 0) break;
    char c = static_cast<char>(value);
    if (c == '\r') continue;

    if (c == '\n') {
      if (!discardLine && lineLength > 0) {
        lineBuffer[lineLength] = '\0';
        Serial.println(lineBuffer);
        processMeasurement(lineBuffer);
        if (liveStream && serverRunning && tcpClient.connected()) {
          tcpClient.println(lineBuffer);
        }
      }
      lineLength = 0;
      discardLine = false;
    } else if (!discardLine) {
      if (c < 32 || c > 126 || lineLength >= sizeof(lineBuffer) - 1) {
        discardLine = true;
        lineLength = 0;
        Serial.println("Gecersiz/uzun UART satiri atlandi.");
      } else {
        lineBuffer[lineLength++] = c;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  stm32Serial.begin(STM32_BAUD, SWSERIAL_8N1, STM32_RX_PIN, -1,
                    false, 512, 4096);
  if (!stm32Serial) {
    Serial.println("D5 UART baslatilamadi.");
    while (true) delay(1000);
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println();
  Serial.println("EnviroGuard: PA2 -> D5, 115200 baud, TCP 8080");
  Serial.println("Wi-Fi baglantisi bekleniyor...");
}

void loop() {
  readSTM32();

  if (WiFi.status() == WL_CONNECTED) {
    if (!serverRunning) {
      tcpServer.begin();
      tcpServer.setNoDelay(true);
      serverRunning = true;
      Serial.print("ESP IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("Hercules: TCP Client, Port 8080, Connect");
    }

    WiFiClient incoming = tcpServer.available();
    if (incoming) {
      if (!tcpClient.connected()) {
        tcpClient.stop();
        tcpClient = incoming;
        tcpClient.setNoDelay(true);
        liveStream = false;
        commandLength = 0;
        discardCommand = false;
        Serial.println("TCP istemcisi baglandi.");
      } else {
        incoming.stop();
      }
    }

    readCommands();
  } else {
    if (serverRunning) {
      tcpClient.stop();
      tcpServer.stop();
      serverRunning = false;
      Serial.println("Wi-Fi koptu; yeniden baglanti bekleniyor.");
    }
    if (millis() - lastWifiReport >= 10000) {
      lastWifiReport = millis();
      Serial.println("Wi-Fi bekleniyor: ag adi, sifre ve 2.4 GHz agi kontrol et.");
    }
  }
  delay(1);
}
