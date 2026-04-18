#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <limits.h>
#include <esp_private/esp_clk.h>

#include "config.h"

SHT85 sht(0x44);  // Direccion I2C del SHT85

volatile uint32_t windRotations = 0;
volatile uint32_t lastWindInterruptMs = 0;
volatile uint32_t lastRainTipMs = 0;
volatile uint32_t rainIntervalMs = 0;
volatile bool hasRainSample = false;

static volatile uint32_t rotations = 0;
static volatile unsigned long ContactBounceTime = 0;
static volatile unsigned long tipTime = 0;
static volatile unsigned long rainTime = 100000;
static volatile uint8_t OverflowCount = 0;
RTC_DATA_ATTR volatile uint32_t accumulatedRainTips = 0;
RTC_DATA_ATTR uint64_t rainDayStartUs = 0;

constexpr uint64_t RAIN_DAY_WINDOW_US = 24ULL * 60ULL * 60ULL * 1000000ULL;

static void updateRainDailyWindow(uint64_t nowUs) {
    if (rainDayStartUs == 0) {
        rainDayStartUs = nowUs;
        return;
    }

    uint64_t elapsedUs = nowUs - rainDayStartUs;
    if (elapsedUs < RAIN_DAY_WINDOW_US) {
        return;
    }

    uint64_t windowsElapsed = elapsedUs / RAIN_DAY_WINDOW_US;
    rainDayStartUs += windowsElapsed * RAIN_DAY_WINDOW_US;
    accumulatedRainTips = 0;
}

static int16_t clampToInt16(long value, const char* field) {
    if (value < INT16_MIN) {
        Serial.printf("[Payload] WARN overflow %s: %ld < %d, clamped\n", field, value, INT16_MIN);
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        Serial.printf("[Payload] WARN overflow %s: %ld > %d, clamped\n", field, value, INT16_MAX);
        return INT16_MAX;
    }
    return static_cast<int16_t>(value);
}

static int32_t clampToInt32(long value, const char* field) {
    if (value < INT32_MIN) {
        Serial.printf("[Payload] WARN overflow %s: %ld < %ld, clamped\n", field, value, static_cast<long>(INT32_MIN));
        return INT32_MIN;
    }
    if (value > INT32_MAX) {
        Serial.printf("[Payload] WARN overflow %s: %ld > %ld, clamped\n", field, value, static_cast<long>(INT32_MAX));
        return INT32_MAX;
    }
    return static_cast<int32_t>(value);
}

static uint8_t getBatteryPercentage(float voltage) {
    // Rango tipico Li-ion: 3.0V (0%) a 4.2V (100%)
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.0f) return 0;
    return static_cast<uint8_t>((voltage - 3.0f) / 1.2f * 100.0f);
}

static void printSensorStatus() {
    Serial.println();
    Serial.println("╔════════════════════════════════╦═════════╗");
    Serial.println("║ SENSOR                         ║ ESTADO  ║");
    Serial.println("╠════════════════════════════════╬═════════╣");
    Serial.printf("║ SHT85 (T/H)                    ║ %s ║\n", ENABLE_SHT85 ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Anemómetro (Velocidad Viento) ║ %s ║\n", ENABLE_WIND_SENSOR ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Veleta (Dirección Viento)      ║ %s ║\n", ENABLE_WIND_VANE ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Pluviómetro (Lluvia)           ║ %s ║\n", ENABLE_RAIN_SENSOR ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Presión                        ║ %s ║\n", ENABLE_PRESSURE ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Altitud                        ║ %s ║\n", ENABLE_ALTITUDE ? "✓ ON " : "✗ OFF");
    Serial.printf("║ Batería                        ║ %s ║\n", ENABLE_BATTERY ? "✓ ON " : "✗ OFF");
    Serial.println("╚════════════════════════════════╩═════════╝");
}

void initSensors() {
    printSensorStatus();

    delay(100);

    if (ENABLE_WIND_SENSOR && WIND_SENSOR_PIN >= 34) {
        Serial.printf("[Sensor] Aviso: GPIO %u (viento) no tiene pull-up interno, usa pull-up externo\n",
                      WIND_SENSOR_PIN);
    }
    if (ENABLE_RAIN_SENSOR && RAIN_SENSOR_PIN >= 34) {
        Serial.printf("[Sensor] Aviso: GPIO %u (lluvia) no tiene resistencias internas, usa pull-down externo\n",
                      RAIN_SENSOR_PIN);
    }

    // Control del amplificador operacional para viento/veleta: se activa solo durante la lectura de esos sensores.
    pinMode(VOLTAGE_PIN, OUTPUT);
    digitalWrite(VOLTAGE_PIN, HIGH);

    delay(100);

    if (ENABLE_WIND_SENSOR) {
        pinMode(WIND_SENSOR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), onWindInterrupt, FALLING);
    }

    if (ENABLE_WIND_VANE) {
        pinMode(WIND_VANE_PIN, INPUT);
        analogReadResolution(12);
        analogSetPinAttenuation(WIND_VANE_PIN, ADC_11db);
    }

    if (ENABLE_RAIN_SENSOR) {
        // En ESP32, los GPIO 34..39 son solo entrada y no soportan pull-up interno.
        // Este pin requiere pull-down externo (ej. 10k a GND) y pulso a VCC al cerrar switch.
        pinMode(RAIN_SENSOR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), onRainInterrupt, RISING);
    }

    if (ENABLE_SHT85) {
        Wire.begin();
        Wire.setClock(100000);

        if (!sht.begin()) {
            Serial.println("[Sensor] ❌ ERROR: SHT85 no inicializado");
            Serial.print("  Código de error: 0x");
            Serial.println(sht.getError(), HEX);
            delay(5000);
            ESP.restart();
        }

        if (!sht.isConnected()) {
            Serial.println("[Sensor] ❌ ERROR: SHT85 no detectado");
            Serial.print("  Código de error: 0x");
            Serial.println(sht.getError(), HEX);
            delay(5000);
            ESP.restart();
        }

        uint32_t serialNumber = 0;
        if (sht.getSerialNumber(serialNumber, true)) {
            Serial.print("  Serial: 0x");
            Serial.println(serialNumber, HEX);
        }

        Serial.println("[Sensor] ✓ SHT85 OK");
        Serial.println();
    }
}

int windDirection() {
    // Asegura alimentacion del operacional antes de muestrear la veleta.
    digitalWrite(VOLTAGE_PIN, HIGH);
    delayMicroseconds(200);

    uint32_t vaneAccumulator = 0;
    constexpr uint8_t kSamples = 8;
    for (uint8_t i = 0; i < kSamples; ++i) {
        vaneAccumulator += static_cast<uint32_t>(analogRead(WIND_VANE_PIN));
    }

    int vaneValue = static_cast<int>(vaneAccumulator / kSamples);
    int mappedDirection = map(vaneValue, 0, 4095, 0, 360);
    mappedDirection = constrain(mappedDirection, 0, 360);
    int windCalDirection = mappedDirection;
    if (windCalDirection > 360) windCalDirection = windCalDirection - 360;
    if (windCalDirection < 0) windCalDirection = windCalDirection + 360;

    String windCompassDirection = " ";
    if (windCalDirection < 22) windCompassDirection = "N";
    else if (windCalDirection < 67) windCompassDirection = "NE";
    else if (windCalDirection < 112) windCompassDirection = "E";
    else if (windCalDirection < 157) windCompassDirection = "SE";
    else if (windCalDirection < 212) windCompassDirection = "S";
    else if (windCalDirection < 247) windCompassDirection = "SW";
    else if (windCalDirection < 292) windCompassDirection = "W";
    else if (windCalDirection < 337) windCompassDirection = "NW";
    else windCompassDirection = "N";

    Serial.print("Wind vane raw(GPIO");
    Serial.print(WIND_VANE_PIN);
    Serial.print("): ");
    Serial.println(vaneValue);
    Serial.print("Wind direction: ");
    Serial.print(windCalDirection);
    Serial.println(windCompassDirection);
    return windCalDirection;
}

float windSpeed() {
    noInterrupts();
    uint32_t currentRotations = rotations;
    rotations = 0;
    windRotations = 0;
    interrupts();

    float computedWindSpeed = static_cast<float>(currentRotations) * 0.9f;
    computedWindSpeed = computedWindSpeed * 1.61f;
    Serial.print("Velocidad del viento =");
    Serial.print(computedWindSpeed);
    Serial.println("km/h");
    return computedWindSpeed;
}

void rotate() {
    unsigned long now = millis();
    if ((now - ContactBounceTime) > 15) {
        rotations++;
        windRotations = rotations;
        ContactBounceTime = now;
        lastWindInterruptMs = now;
    }
}

float rainRate() {
    unsigned long currentRainTime;
    noInterrupts();
    currentRainTime = rainTime;
    interrupts();

    if (currentRainTime == 0) {
        return 0.0f;
    }

    float precipitacion = (0.2f * 3600000.0f) / static_cast<float>(currentRainTime);
    Serial.print("Rainrate");
    Serial.print(precipitacion);

    return precipitacion;
}

void rain() {
    updateRainDailyWindow(esp_clk_rtc_time());

    Serial.println("RainCount");
    long currentTime = millis();
    if (tipTime != 0 && (currentTime - static_cast<long>(tipTime)) < 2000) {
        return;
    }

    if (OverflowCount == 0) {
        rainTime = 100000;
        OverflowCount = 1;
    } else {
        rainTime = static_cast<unsigned long>(currentTime - static_cast<long>(tipTime));
    }

    tipTime = static_cast<unsigned long>(currentTime);
    accumulatedRainTips++;

    lastRainTipMs = static_cast<uint32_t>(tipTime);
    rainIntervalMs = static_cast<uint32_t>(rainTime);
    hasRainSample = true;
}

uint32_t getRainTipsAccumulated() {
    updateRainDailyWindow(esp_clk_rtc_time());

    noInterrupts();
    uint32_t tips = accumulatedRainTips;
    interrupts();
    return tips;
}

float getRainAccumulatedMm() {
    return static_cast<float>(getRainTipsAccumulated()) * RAIN_MM_PER_TIP;
}

void IRAM_ATTR onWindInterrupt() {
    rotate();
}

void IRAM_ATTR onRainInterrupt() {
    rain();
}

float readBatteryVoltage() {
    int raw = analogRead(BATTERY_PIN);
    float sensed = (static_cast<float>(raw) * 2.1f) / 2500.0f;
    float battery = sensed * 2.0f;
    return battery;
}

float readWindDirection() {
    return static_cast<float>(windDirection());
}

float readWindSpeedKmh() {
    return windSpeed();
}

float readRainRateMmH() {
    return rainRate();
}

void readSensors(SensorPayload& p) {
    float battery = ENABLE_BATTERY ? readBatteryVoltage() : 0.0f;
    uint8_t batteryPercent = getBatteryPercentage(battery);

    float temperature = 0.0f;
    float hum = 0.0f;

    if (ENABLE_SHT85) {
        if (!sht.read(false)) {
            // Error silencioso
        } else {
            temperature = sht.getTemperature();
            hum = sht.getHumidity();
        }
    }

    float pressure = ENABLE_PRESSURE ? 0.0f : 0.0f;
    float altitude = ENABLE_ALTITUDE ? 0.0f : 0.0f;
    float windDir = ENABLE_WIND_VANE ? static_cast<float>(windDirection()) : 0.0f;
    float windSpeedValue = ENABLE_WIND_SENSOR ? windSpeed() : 0.0f;
    float rainRateValue = ENABLE_RAIN_SENSOR ? rainRate() : 0.0f;
    float rainAccumulatedValue = ENABLE_RAIN_SENSOR ? getRainAccumulatedMm() : 0.0f;

    Serial.println();
    Serial.println("╔════════════════════════════════╦══════════════════╗");
    Serial.println("║ SENSOR                         ║ VALOR            ║");
    Serial.println("╠════════════════════════════════╬══════════════════╣");

    if (ENABLE_SHT85) {
        Serial.printf("║ Temperatura                    ║ %14.1f°C ║\n", temperature);
        Serial.printf("║ Humedad                        ║ %14.1f%% ║\n", hum);
    }

    if (ENABLE_BATTERY) {
        Serial.printf("║ Batería                        ║ %14d%% ║\n", batteryPercent);
    }

    if (ENABLE_WIND_SENSOR) {
        Serial.printf("║ Velocidad Viento               ║ %14.1f    ║\n", windSpeedValue);
    }

    if (ENABLE_WIND_VANE) {
        Serial.printf("║ Dirección Viento               ║ %14.0f°   ║\n", windDir);
    }

    if (ENABLE_RAIN_SENSOR) {
        Serial.printf("║ Tasa de Lluvia                 ║ %14.2f    ║\n", rainRateValue);
        Serial.printf("║ Lluvia acumulada (24h)         ║ %14.2f mm ║\n", rainAccumulatedValue);
    }

    Serial.println("╚════════════════════════════════╩══════════════════╝");

    p.temperature_c_x100 = clampToInt16(lroundf(temperature * 100.0f), "temperature_c_x100");
    p.pressure_x100 = clampToInt32(lroundf(pressure * 100.0f), "pressure_x100");
    p.altitude_m_x100 = clampToInt16(lroundf(altitude * 100.0f), "altitude_m_x100");
    p.humidity_pct_x100 = clampToInt16(lroundf(hum * 100.0f), "humidity_pct_x100");
    p.wind_dir_deg_x100 = clampToInt32(lroundf(windDir * 100.0f), "wind_dir_deg_x100");
    p.wind_speed_kmh_x100 = clampToInt16(lroundf(windSpeedValue * 100.0f), "wind_speed_kmh_x100");
    p.rain_rate_x10 = clampToInt32(lroundf(rainRateValue * 100.0f), "rain_rate_x10");
    p.rain_accum_x10 = clampToInt32(lroundf(rainAccumulatedValue * 10.0f), "rain_accum_x10");

    int batteryLegacy = static_cast<int>(battery);
    int batteryScaled = batteryLegacy * 10;
    if (batteryScaled < 0) batteryScaled = 0;
    if (batteryScaled > 255) batteryScaled = 255;
    p.battery_x10 = static_cast<uint8_t>(batteryScaled);
}
