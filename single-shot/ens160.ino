#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "secrets.h"

// --- NEW: OLED includes ---
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- BMP280: include ---
#include <Adafruit_BMP280.h>

// macro definitions
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

SensirionI2cScd4x sensor;

// --- BMP280: instance + state ---
Adafruit_BMP280 bmp;  // I2C
bool bmpOk = false;

static char errorMessage[64];
static int16_t error;

// --- NEW: OLED config ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Reset pin is often not connected on I2C OLEDs; use -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
const bool ALLOW_INSECURE_TLS = true;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long UPLOAD_INTERVAL_MS = 60000;
const int MAX_NETWORKS_TO_LOG = 10;

unsigned long lastUploadAttemptMs = 0;
bool wifiConnectionStarted = false;
char uploadStatusLine[24] = "Up: idle";

void PrintUint64(uint64_t& value) {
    Serial.print("0x");
    Serial.print((uint32_t)(value >> 32), HEX);
    Serial.print((uint32_t)(value & 0xFFFFFFFF), HEX);
}

const char* wifiStatusToString(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD:
            return "NO_SHIELD";
        case WL_IDLE_STATUS:
            return "IDLE";
        case WL_NO_SSID_AVAIL:
            return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:
            return "SCAN_COMPLETED";
        case WL_CONNECTED:
            return "CONNECTED";
        case WL_CONNECT_FAILED:
            return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

bool wifiNeedsNewBegin(wl_status_t status) {
    return status == WL_NO_SSID_AVAIL ||
           status == WL_CONNECT_FAILED ||
           status == WL_CONNECTION_LOST ||
           status == WL_DISCONNECTED;
}

void logWifiCredentials() {
    Serial.print("WiFi SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("WiFi password: ");
    Serial.println(WIFI_PASSWORD);
}

void printVisibleNetworks() {
    Serial.println("Scanning for nearby WiFi networks...");

    int networkCount = WiFi.scanNetworks(false, true);
    if (networkCount <= 0) {
        Serial.println("No WiFi networks found");
        WiFi.scanDelete();
        return;
    }

    Serial.print("Nearby WiFi networks: ");
    Serial.println(networkCount);

    int networksToLog = networkCount < MAX_NETWORKS_TO_LOG ? networkCount : MAX_NETWORKS_TO_LOG;
    for (int i = 0; i < networksToLog; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            ssid = "<hidden>";
        }

        Serial.print("  ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(ssid);
        Serial.print(" RSSI=");
        Serial.print(WiFi.RSSI(i));
        Serial.print(" dBm channel=");
        Serial.println(WiFi.channel(i));
    }

    if (networkCount > networksToLog) {
        Serial.println("  ...");
    }

    WiFi.scanDelete();
}

void startWifiConnection() {
    Serial.println("Starting WiFi connection");
    logWifiCredentials();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiConnectionStarted = true;
}

bool ensureWifiConnected(unsigned long timeoutMs) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        return true;
    }

    if (!wifiConnectionStarted || wifiNeedsNewBegin(status)) {
        if (wifiConnectionStarted) {
            Serial.print("Restarting WiFi after status: ");
            Serial.println(wifiStatusToString(status));
        }
        startWifiConnection();
    } else {
        Serial.print("WiFi still connecting, status: ");
        Serial.println(wifiStatusToString(status));
    }

    unsigned long startedAt = millis();
    wl_status_t lastLoggedStatus = WiFi.status();
    while ((status = WiFi.status()) != WL_CONNECTED && millis() - startedAt < timeoutMs) {
        if (status != lastLoggedStatus) {
            Serial.print("WiFi status: ");
            Serial.println(wifiStatusToString(status));
            lastLoggedStatus = status;
        }
        delay(250);
    }

    status = WiFi.status();
    if (status == WL_CONNECTED) {
        return true;
    }

    Serial.print("WiFi connect timed out, final status: ");
    Serial.println(wifiStatusToString(status));
    if (status == WL_NO_SSID_AVAIL) {
        printVisibleNetworks();
    }
    WiFi.disconnect(true, false);
    wifiConnectionStarted = false;
    return false;
}

bool shouldUploadNow() {
    if (lastUploadAttemptMs == 0) {
        return true;
    }

    return millis() - lastUploadAttemptMs >= UPLOAD_INTERVAL_MS;
}

void setUploadStatus(const char* status) {
    snprintf(uploadStatusLine, sizeof(uploadStatusLine), "%s", status);
}

bool uploadReading(uint16_t co2Concentration,
                   float temperature,
                   float relativeHumidity,
                   bool hasPressure,
                   float pressure_hPa) {

    if (!ensureWifiConnected(WIFI_CONNECT_TIMEOUT_MS)) {
        setUploadStatus("Up: WiFi down");
        return false;
    }

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    const bool useTls = strncmp(API_URL, "https://", 8) == 0;

    if (useTls) {
        if (ALLOW_INSECURE_TLS) {
            // Fine for bring-up; swap to setCACert() when you want proper verification.
            secureClient.setInsecure();
        }

        if (!http.begin(secureClient, API_URL)) {
            setUploadStatus("Up: begin fail");
            return false;
        }
    } else {
        if (!http.begin(plainClient, API_URL)) {
            setUploadStatus("Up: begin fail");
            return false;
        }
    }

    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    if (strlen(API_KEY) > 0) {
        http.addHeader("X-API-Key", API_KEY);
    }

    char payload[160];
    if (hasPressure) {
        snprintf(payload,
                 sizeof(payload),
                 "{\"co2\":%u,\"temp\":%.2f,\"rh\":%.2f,\"pressure\":%.2f}",
                 co2Concentration,
                 temperature,
                 relativeHumidity,
                 pressure_hPa);
    } else {
        snprintf(payload,
                 sizeof(payload),
                 "{\"co2\":%u,\"temp\":%.2f,\"rh\":%.2f}",
                 co2Concentration,
                 temperature,
                 relativeHumidity);
    }

    int statusCode = http.POST((uint8_t*)payload, strlen(payload));
    String responseBody = http.getString();
    http.end();

    Serial.print("Upload status: ");
    Serial.println(statusCode);
    if (responseBody.length() > 0) {
        Serial.println(responseBody);
    }

    if (statusCode >= 200 && statusCode < 300) {
        snprintf(uploadStatusLine, sizeof(uploadStatusLine), "Up: HTTP %d", statusCode);
        return true;
    }

    snprintf(uploadStatusLine, sizeof(uploadStatusLine), "Up: HTTP %d", statusCode);
    return false;
}

void setup() {

    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }

    Wire.begin();

    // --- BMP280: init BEFORE we get too far ---
    // Most BMP280 breakouts default to 0x76, but some use 0x77.
    if (bmp.begin(0x76)) {
        Serial.println("BMP280 found at 0x76");
        bmpOk = true;
    } else if (bmp.begin(0x77)) {
        Serial.println("BMP280 found at 0x77");
        bmpOk = true;
    } else {
        Serial.println("BMP280 not found (check wiring / address)");
        bmpOk = false;
    }

    sensor.begin(Wire, SCD41_I2C_ADDR_62);

    // --- NEW: init OLED ---
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // change to 0x3D if needed
        Serial.println("SSD1306 allocation failed");
        while (true) {
            delay(1000);
        }
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("OLED OK");
    if (bmpOk) {
        display.println("BMP280 OK");
    } else {
        display.println("BMP280 FAIL");
    }
    display.println("Waiting for SCD41...");
    display.display();
    // --- END OLED init ---

    if (ensureWifiConnected(WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        setUploadStatus("Up: ready");
    } else {
        Serial.println("WiFi not connected yet");
        setUploadStatus("Up: WiFi down");
    }

    uint64_t serialNumber = 0;
    delay(30);

    // Ensure sensor is in clean state
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute wakeUp(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    error = sensor.stopPeriodicMeasurement();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    error = sensor.reinit();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute reinit(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }

    // Read out information about the sensor
    error = sensor.getSerialNumber(serialNumber);
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }
    Serial.print("serial number: ");
    PrintUint64(serialNumber);
    Serial.println();

    // Update OLED with serial read success
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OLED OK");
    display.println("SCD41 OK");
    if (bmpOk) {
        display.println("BMP280 OK");
    } else {
        display.println("BMP280 FAIL");
    }
    display.print("WiFi: ");
    display.println(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    display.display();
}

void loop() {

    uint16_t co2Concentration = 0;
    float temperature = 0.0;
    float relativeHumidity = 0.0;

    // --- BMP280: read pressure ---
    float pressure_hPa = NAN;
    if (bmpOk) {
        // Adafruit BMP280 returns pressure in Pa; convert to hPa.
        pressure_hPa = bmp.readPressure() / 100.0F;
    }

    // Wake the sensor up from sleep mode.
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute wakeUp(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }

    // Ignore first measurement after wake up.
    error = sensor.measureSingleShot();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute measureSingleShot(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }

    // Perform single shot measurement and read data.
    error = sensor.measureAndReadSingleShot(co2Concentration,
                                            temperature,
                                            relativeHumidity);
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute measureAndReadSingleShot(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }

    // Print results in physical units (Serial)
    Serial.print("CO2 concentration [ppm]: ");
    Serial.println(co2Concentration);

    Serial.print("Temperature [°C]: ");
    Serial.println(temperature);

    Serial.print("Relative Humidity [%RH]: ");
    Serial.println(relativeHumidity);

    if (bmpOk) {
        Serial.print("Pressure [hPa]: ");
        Serial.println(pressure_hPa);
    } else {
        Serial.println("Pressure: BMP280 not available");
    }

    Serial.println("sleep for 5 seconds until next measurement is due");

    uint16_t ascEnabled = 9999;
    sensor.setAutomaticSelfCalibrationEnabled(0);
    error = sensor.getAutomaticSelfCalibrationEnabled(ascEnabled);
    if (error == NO_ERROR) {
        Serial.print("ASC enabled? ");
        Serial.println(ascEnabled == 1 ? "yes" : "no");
    }

    if (shouldUploadNow()) {
        lastUploadAttemptMs = millis();
        uploadReading(co2Concentration,
                      temperature,
                      relativeHumidity,
                      bmpOk,
                      pressure_hPa);
    }

    // --- NEW: OLED output ---
    display.clearDisplay();
    display.setCursor(0, 0);

    display.print("CO2: ");
    display.print(co2Concentration);
    display.println(" ppm");

    display.print("T: ");
    display.print(temperature, 1);
    display.println(" C");

    display.print("RH: ");
    display.print(relativeHumidity, 1);
    display.println(" %");

    if (bmpOk) {
        display.print("P: ");
        display.print(pressure_hPa, 1);
        display.println(" hPa");
    } else {
        display.println("P: ---");
    }

    display.print("WiFi: ");
    display.println(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");

    display.display();
    // --- END OLED output ---

    delay(5000);
}
