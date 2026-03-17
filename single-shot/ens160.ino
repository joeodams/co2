#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

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

void PrintUint64(uint64_t& value) {
    Serial.print("0x");
    Serial.print((uint32_t)(value >> 32), HEX);
    Serial.print((uint32_t)(value & 0xFFFFFFFF), HEX);
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
    Serial.print("serial number: 0x");
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

    display.display();
    // --- END OLED output ---

    delay(5000);
}
