#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <limits.h>
#include <esp_private/esp_clk.h>

#include "config.h"

SHT85 sht(SHT_ADDRESS);  // Direccion I2C del SHT85

volatile uint32_t windRotations = 0;
volatile uint32_t lastWindInterruptMs = 0;
volatile uint32_t lastRainTipMs = 0;
volatile uint32_t rainIntervalMs = 0;
volatile bool hasRainSample = false;

static volatile uint32_t rotations = 0;
static volatile unsigned long contactBounceTime = 0;
static volatile uint32_t pendingRainInterrupts = 0;
static volatile uint32_t pendingWindInterrupts = 0;
static bool shtReady = false;
static uint32_t lastWindSpeedSampleMs = 0;
static uint32_t lastRainOverflowWarnMs = 0;
static uint32_t lastWindOverflowWarnMs = 0;

constexpr uint32_t MAX_INTERRUPT_EVENTS_PER_CYCLE = 256;

RTC_DATA_ATTR volatile uint32_t accumulatedRainTips = 0;
RTC_DATA_ATTR uint64_t rainDayStartUs = 0;
RTC_DATA_ATTR uint64_t lastRainTipUs = 0;
RTC_DATA_ATTR uint64_t rainTipHistoryUs[RAIN_TIP_HISTORY_SIZE] = {0};
RTC_DATA_ATTR uint8_t rainTipHistoryCount = 0;
RTC_DATA_ATTR uint8_t rainTipHistoryHead = 0;

constexpr uint64_t RAIN_DAY_WINDOW_US = 24ULL * 60ULL * 60ULL * 1000000ULL;
constexpr uint64_t RAIN_RATE_AVG_WINDOW_US =
    static_cast<uint64_t>(RAIN_RATE_AVG_WINDOW_MS) * 1000ULL;

static void clearRainTipHistory() {
    for (uint8_t i = 0; i < RAIN_TIP_HISTORY_SIZE; ++i) {
        rainTipHistoryUs[i] = 0;
    }
    rainTipHistoryCount = 0;
    rainTipHistoryHead = 0;
}

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
    lastRainTipUs = 0;
    rainIntervalMs = 0;
    hasRainSample = false;
    clearRainTipHistory();
}

static void pushRainTipHistory(uint64_t tsUs) {
    rainTipHistoryUs[rainTipHistoryHead] = tsUs;
    rainTipHistoryHead = (rainTipHistoryHead + 1) % RAIN_TIP_HISTORY_SIZE;
    if (rainTipHistoryCount < RAIN_TIP_HISTORY_SIZE) {
        rainTipHistoryCount++;
    }
}

static uint8_t countRainTipsInWindow(uint64_t nowUs) {
    if (rainTipHistoryCount == 0) {
        return 0;
    }

    uint8_t tips = 0;
    for (uint8_t i = 0; i < rainTipHistoryCount; ++i) {
        uint8_t idx = (rainTipHistoryHead + RAIN_TIP_HISTORY_SIZE - 1 - i) % RAIN_TIP_HISTORY_SIZE;
        uint64_t ts = rainTipHistoryUs[idx];
        if (ts == 0) {
            break;
        }
        if (nowUs >= ts && (nowUs - ts) <= RAIN_RATE_AVG_WINDOW_US) {
            tips++;
        } else if (nowUs > ts && (nowUs - ts) > RAIN_RATE_AVG_WINDOW_US) {
            break;
        }
    }
    return tips;
}

static bool registerRainTip(uint64_t nowUs) {
    updateRainDailyWindow(nowUs);

    const uint64_t debounceUs = static_cast<uint64_t>(RAIN_IGNORE_MS) * 1000ULL;
    if (lastRainTipUs != 0) {
        const uint64_t deltaUs = nowUs - lastRainTipUs;
        if (deltaUs < debounceUs) {
            return false;
        }

        const uint64_t deltaMs = deltaUs / 1000ULL;
        rainIntervalMs = static_cast<uint32_t>(
            deltaMs > static_cast<uint64_t>(UINT32_MAX)
                ? static_cast<uint64_t>(UINT32_MAX)
                : deltaMs
        );
        hasRainSample = true;
    } else {
        rainIntervalMs = 0;
        hasRainSample = false;
    }

    lastRainTipUs = nowUs;
    accumulatedRainTips++;
    pushRainTipHistory(nowUs);

    const unsigned long nowMs = millis();
    lastRainTipMs = static_cast<uint32_t>(nowMs);
    return true;
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
    if (voltage <= 0.0f) return 0;
    if (voltage >= 4.2f) return 100;
    return static_cast<uint8_t>(lroundf((voltage / 4.2f) * 100.0f));
}

static void printSensorStatus() {
    Serial.println();
    Serial.println("+--------------------------------+---------+");
    Serial.println("| SENSOR                         | ESTADO  |");
    Serial.println("+--------------------------------+---------+");
    Serial.printf("| SHT85 (T/H)                    | %-7s |\n", ENABLE_SHT85 ? "ON" : "OFF");
    Serial.printf("| Anemometro (Velocidad Viento)  | %-7s |\n", ENABLE_WIND_SENSOR ? "ON" : "OFF");
    Serial.printf("| Veleta (Direccion Viento)      | %-7s |\n", ENABLE_WIND_VANE ? "ON" : "OFF");
    Serial.printf("| Pluviometro (Lluvia)           | %-7s |\n", ENABLE_RAIN_SENSOR ? "ON" : "OFF");
    Serial.printf("| Presion                        | %-7s |\n", ENABLE_PRESSURE ? "ON" : "OFF");
    Serial.printf("| Altitud                        | %-7s |\n", ENABLE_ALTITUDE ? "ON" : "OFF");
    Serial.printf("| Bateria                        | %-7s |\n", ENABLE_BATTERY ? "ON" : "OFF");
    Serial.println("+--------------------------------+---------+");
}

void initSensors() {
    printSensorStatus();
    delay(100);

    const bool windCanUseInterrupt =
        ENABLE_WIND_SENSOR &&
        ((WIND_SENSOR_PIN < 34) || WIND_SENSOR_HAS_EXTERNAL_BIAS);
    const bool rainCanUseInterrupt =
        ENABLE_RAIN_SENSOR &&
        ((RAIN_SENSOR_PIN < 34) || RAIN_SENSOR_HAS_EXTERNAL_BIAS);

    if (ENABLE_WIND_SENSOR && WIND_SENSOR_PIN >= 34 && !WIND_SENSOR_HAS_EXTERNAL_BIAS) {
        Serial.printf("[Sensor] Aviso: GPIO %u (viento) no tiene pull-up interno, usa pull-up externo\n",
                      WIND_SENSOR_PIN);
        Serial.println("[Sensor] WIND IRQ deshabilitada para evitar ruido/reinicios");
    }
    if (ENABLE_RAIN_SENSOR && RAIN_SENSOR_PIN >= 34 && !RAIN_SENSOR_HAS_EXTERNAL_BIAS) {
        Serial.printf("[Sensor] Aviso: GPIO %u (lluvia) no tiene resistencias internas, usa pull-down externo\n",
                      RAIN_SENSOR_PIN);
        Serial.println("[Sensor] RAIN IRQ deshabilitada para evitar ruido/reinicios");
    }

    // Control del operacional para viento/veleta.
    pinMode(VOLTAGE_PIN, OUTPUT);
    digitalWrite(VOLTAGE_PIN, HIGH);

    if (windCanUseInterrupt) {
        pinMode(WIND_SENSOR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(WIND_SENSOR_PIN), onWindInterrupt, FALLING);
    }

    if (ENABLE_WIND_VANE) {
        pinMode(WIND_VANE_PIN, INPUT);
        analogReadResolution(12);
        analogSetPinAttenuation(WIND_VANE_PIN, ADC_11db);
    }

    if (rainCanUseInterrupt) {
        pinMode(RAIN_SENSOR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), onRainInterrupt, RISING);
    }

    if (ENABLE_BATTERY) {
        pinMode(BATTERY_PIN, INPUT);
        analogReadResolution(12);
        analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
    }

    if (ENABLE_SHT85) {
        // El bus del SHT85 siempre se inicializa con pines explicitos:
        // no debe depender del modo debug ni del estado de la OLED.
        Wire.begin(SHT_SDA_PIN, SHT_SCL_PIN);
        Wire.setClock(SHT_I2C_CLOCK_HZ);

        shtReady = false;
        for (uint8_t attempt = 1; attempt <= SHT_INIT_RETRIES; ++attempt) {
            const bool beginOk = sht.begin();
            const bool connected = beginOk && sht.isConnected();
            if (connected) {
                sht.clearStatus();
                shtReady = true;
                break;
            }

            Wire.begin(SHT_SDA_PIN, SHT_SCL_PIN);
            Wire.setClock(SHT_I2C_CLOCK_HZ);

            Serial.printf("[Sensor] WARN SHT85 init intento %u/%u fallo (error 0x%02X)\n",
                          static_cast<unsigned>(attempt),
                          static_cast<unsigned>(SHT_INIT_RETRIES),
                          static_cast<unsigned>(sht.getError()));
            delay(120);
        }

        if (!shtReady) {
            Serial.println("[Sensor] ERROR: SHT85 no inicializado, se continua sin T/H");
        } else {
            uint32_t serialNumber = 0;
            if (sht.getSerialNumber(serialNumber, false)) {
                Serial.print("[Sensor] SHT85 serial: 0x");
                Serial.println(serialNumber, HEX);
            }
            Serial.println("[Sensor] SHT85 OK");
        }
    }

    Serial.println();
}

int windDirection() {
    digitalWrite(VOLTAGE_PIN, HIGH);
    delayMicroseconds(200);

    uint32_t vaneAccumulator = 0;
    constexpr uint8_t kSamples = 8;
    for (uint8_t i = 0; i < kSamples; ++i) {
        vaneAccumulator += static_cast<uint32_t>(analogRead(WIND_VANE_PIN));
    }

    int vaneValue = static_cast<int>(vaneAccumulator / kSamples);
    return constrain(map(vaneValue, 0, 4095, 0, 360), 0, 360);
}

float windSpeed() {
    uint32_t pendingWind = 0;
    bool windOverflow = false;
    noInterrupts();
    pendingWind = pendingWindInterrupts;
    if (pendingWind > MAX_INTERRUPT_EVENTS_PER_CYCLE) {
        pendingWindInterrupts -= MAX_INTERRUPT_EVENTS_PER_CYCLE;
        pendingWind = MAX_INTERRUPT_EVENTS_PER_CYCLE;
        windOverflow = true;
    } else {
        pendingWindInterrupts = 0;
    }
    interrupts();

    while (pendingWind > 0) {
        --pendingWind;
        rotate();
    }

    const uint32_t nowMs = millis();

    noInterrupts();
    uint32_t currentRotations = rotations;
    rotations = 0;
    windRotations = 0;
    interrupts();

    uint32_t elapsedMs = 0;
    if (lastWindSpeedSampleMs != 0 && nowMs >= lastWindSpeedSampleMs) {
        elapsedMs = nowMs - lastWindSpeedSampleMs;
    }
    lastWindSpeedSampleMs = nowMs;

    if (elapsedMs == 0 || currentRotations == 0) {
        return 0.0f;
    }

    const float rotationsPerSecond =
        (static_cast<float>(currentRotations) * 1000.0f) / static_cast<float>(elapsedMs);
    float computedWindSpeed = rotationsPerSecond * WIND_FACTOR;

    if (windOverflow && (nowMs - lastWindOverflowWarnMs) > 2000) {
        Serial.println("[Wind] WARN demasiadas interrupciones, se limita procesamiento por ciclo");
        lastWindOverflowWarnMs = nowMs;
    }

    return computedWindSpeed * MPH_TO_KMH;
}

void rotate() {
    unsigned long now = millis();
    if ((now - contactBounceTime) > WIND_DEBOUNCE_MS) {
        rotations++;
        windRotations = rotations;
        contactBounceTime = now;
        lastWindInterruptMs = now;
    }
}

float rainRate() {
    uint64_t nowUs = esp_clk_rtc_time();
    updateRainDailyWindow(nowUs);

    uint8_t tips = 0;
    uint32_t intervalMs = 0;
    bool hasSample = false;
    noInterrupts();
    tips = countRainTipsInWindow(nowUs);
    intervalMs = rainIntervalMs;
    hasSample = hasRainSample;
    interrupts();

    if (tips == 0) {
        return 0.0f;
    }

    if (hasSample && intervalMs > 0) {
        const float hoursByInterval = static_cast<float>(intervalMs) / 3600000.0f;
        if (hoursByInterval > 0.0f) {
            return RAIN_MM_PER_TIP / hoursByInterval;
        }
    }

    const float hours = static_cast<float>(RAIN_RATE_AVG_WINDOW_MS) / 3600000.0f;
    if (hours <= 0.0f) {
        return 0.0f;
    }

    return (static_cast<float>(tips) * RAIN_MM_PER_TIP) / hours;
}

bool rain() {
    return registerRainTip(esp_clk_rtc_time());
}

uint32_t processRainInterrupts() {
    uint32_t pendingTips = 0;
    uint32_t appliedTips = 0;
    bool rainOverflow = false;
    noInterrupts();
    pendingTips = pendingRainInterrupts;
    if (pendingTips > MAX_INTERRUPT_EVENTS_PER_CYCLE) {
        pendingRainInterrupts -= MAX_INTERRUPT_EVENTS_PER_CYCLE;
        pendingTips = MAX_INTERRUPT_EVENTS_PER_CYCLE;
        rainOverflow = true;
    } else {
        pendingRainInterrupts = 0;
    }
    interrupts();

    while (pendingTips > 0) {
        --pendingTips;
        if (rain()) {
            appliedTips++;
        }
    }

    if (rainOverflow) {
        const uint32_t nowMs = millis();
        if ((nowMs - lastRainOverflowWarnMs) > 2000) {
            Serial.println("[Rain] WARN demasiadas interrupciones, se limita procesamiento por ciclo");
            lastRainOverflowWarnMs = nowMs;
        }
    }

    return appliedTips;
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
    pendingWindInterrupts++;
}

void IRAM_ATTR onRainInterrupt() {
    pendingRainInterrupts++;
}

float readBatteryVoltage() {
    uint32_t rawSum = 0;
    for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; ++i) {
        rawSum += static_cast<uint32_t>(analogRead(BATTERY_PIN));
        delayMicroseconds(200);
    }

    const float rawAvg = static_cast<float>(rawSum) / static_cast<float>(BATTERY_ADC_SAMPLES);
    const float adcVoltage = (rawAvg / BATTERY_ADC_MAX_READING) * BATTERY_ADC_VREF;

    float batteryVoltage =
        (adcVoltage * BATTERY_DIVIDER_RATIO * BATTERY_CALIBRATION_FACTOR) +
        BATTERY_CALIBRATION_OFFSET_V;

    if (batteryVoltage < 0.0f) batteryVoltage = 0.0f;
    if (batteryVoltage > 4.2f) batteryVoltage = 4.2f;
    return batteryVoltage;
}

static void printSensorTable(float temperature,
                             float hum,
                             float battery,
                             uint8_t batteryPercent,
                             float windSpeedValue,
                             float windDir,
                             float rainRateValue,
                             float rainAccumulatedValue) {
    Serial.println();
    Serial.println("+--------------------------------+---------+------------------+");
    Serial.println("| SENSOR                         | ESTADO  | VALOR            |");
    Serial.println("+--------------------------------+---------+------------------+");

    if (ENABLE_SHT85) {
        Serial.printf("| Temperatura                    | %-7s | %16.2f |\n", "ON", temperature);
        Serial.printf("| Humedad                        | %-7s | %16.2f |\n", "ON", hum);
    }

    if (ENABLE_BATTERY) {
        Serial.printf("| Bateria (V)                    | %-7s | %16.3f |\n", "ON", battery);
        Serial.printf("| Bateria (%%)                    | %-7s | %16u |\n", "ON", static_cast<unsigned>(batteryPercent));
    }

    if (ENABLE_WIND_SENSOR) {
        Serial.printf("| Velocidad Viento (km/h)        | %-7s | %16.2f |\n", "ON", windSpeedValue);
    }

    if (ENABLE_WIND_VANE) {
        Serial.printf("| Direccion Viento (deg)         | %-7s | %16.1f |\n", "ON", windDir);
    } else {
        Serial.printf("| Direccion Viento (deg)         | %-7s | %16s |\n", "OFF", "-");
    }

    if (ENABLE_RAIN_SENSOR) {
        Serial.printf("| Lluvia media (mm/h)            | %-7s | %16.2f |\n", "ON", rainRateValue);
        Serial.printf("| Lluvia acumulada 24h (mm)      | %-7s | %16.2f |\n", "ON", rainAccumulatedValue);
    }

    Serial.println("+--------------------------------+---------+------------------+");
}

void readSensors(SensorPayload& p, bool printTable) {
    float battery = ENABLE_BATTERY ? readBatteryVoltage() : 0.0f;
    uint8_t batteryPercent = getBatteryPercentage(battery);

    float temperature = 0.0f;
    float hum = 0.0f;

    if (ENABLE_SHT85) {
        static uint32_t lastRetryMs = 0;
        if (!shtReady) {
            uint32_t nowMs = millis();
            if ((nowMs - lastRetryMs) > 5000) {
                lastRetryMs = nowMs;
                Wire.begin(SHT_SDA_PIN, SHT_SCL_PIN);
                Wire.setClock(SHT_I2C_CLOCK_HZ);
                shtReady = sht.begin() && sht.isConnected();
            }
        }

        if (shtReady && sht.read(false)) {
            temperature = sht.getTemperature();
            hum = sht.getHumidity();
        } else if (shtReady && !sht.isConnected()) {
            shtReady = false;
        }
    }

    const float pressure = 0.0f;
    const float altitude = 0.0f;
    float windDir = ENABLE_WIND_VANE ? static_cast<float>(windDirection()) : 0.0f;
    float windSpeedValue = ENABLE_WIND_SENSOR ? windSpeed() : 0.0f;
    float rainRateValue = ENABLE_RAIN_SENSOR ? rainRate() : 0.0f;
    float rainAccumulatedValue = ENABLE_RAIN_SENSOR ? getRainAccumulatedMm() : 0.0f;

    if (printTable) {
        printSensorTable(
            temperature,
            hum,
            battery,
            batteryPercent,
            windSpeedValue,
            windDir,
            rainRateValue,
            rainAccumulatedValue
        );
    }

    p.temperature_c_x100 = clampToInt16(lroundf(temperature * 100.0f), "temperature_c_x100");
    p.pressure_x100 = clampToInt32(lroundf(pressure * 100.0f), "pressure_x100");
    p.altitude_m_x100 = clampToInt16(lroundf(altitude * 100.0f), "altitude_m_x100");
    p.humidity_pct_x100 = clampToInt16(lroundf(hum * 100.0f), "humidity_pct_x100");
    p.wind_dir_deg_x100 = clampToInt32(lroundf(windDir * 100.0f), "wind_dir_deg_x100");
    p.wind_speed_kmh_x100 = clampToInt16(lroundf(windSpeedValue * 100.0f), "wind_speed_kmh_x100");
    // Mantener compatibilidad con el decoder legacy del servidor.
    p.rain_rate_x10 = clampToInt32(lroundf(rainRateValue * 100.0f), "rain_rate_x10");
    p.rain_accum_x10 = clampToInt32(lroundf(rainAccumulatedValue * 10.0f), "rain_accum_x10");

    int batteryScaled = static_cast<int>(lroundf(battery * 10.0f));
    if (batteryScaled < 0) batteryScaled = 0;
    if (batteryScaled > 255) batteryScaled = 255;
    p.battery_x10 = static_cast<uint8_t>(batteryScaled);
}
