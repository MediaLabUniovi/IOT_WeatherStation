#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <Wire.h>

#include <lmic.h>
#include <hal/hal.h>

#include "credentials.h"
#include "config.h"

const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,
    .dio = {26, 33, 32}
};

SensorPayload payload;
unsigned long lastMeasure = 0;
uint32_t loraTransmitStartTime = 0;

// Cambiar a 1 para habilitar modo debug con pantalla.
#define DEBUG_MODE 1

// En modo debug no hay deep sleep ni envio LoRa.
constexpr bool DEBUG_MODE_ENABLED = (DEBUG_MODE == 1);
constexpr uint32_t DEBUG_REFRESH_MS = 500;
bool g_debugDisplayMode = DEBUG_MODE_ENABLED;

#if DEBUG_MODE
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET = -1;
constexpr uint8_t OLED_I2C_ADDR_PRIMARY = 0x3C;
constexpr uint8_t OLED_I2C_ADDR_SECONDARY = 0x3D;
constexpr uint8_t OLED_SDA_PIN = 21;
constexpr uint8_t OLED_SCL_PIN = 22;
constexpr uint8_t OLED_RST_PIN = 16;
// En esta placa el GPIO4 se usa para la veleta.
// Evitar usarlo como power de OLED para no interferir con sensores.
constexpr int OLED_POWER_PIN = -1;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayReady = false;
#endif

// =========================
// Credenciales LMIC
// =========================
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// =========================
// Utilidades
// =========================
void printHex2(unsigned v) {
    v &= 0xFF;
    if (v < 16) Serial.print('0');
    Serial.print(v, HEX);
}

static float payloadToFloat(int32_t value, float scale) {
    return static_cast<float>(value) / scale;
}

static void printDebugDisplayToSerial(const SensorPayload& values) {
    const float temperature = payloadToFloat(values.temperature_c_x100, 100.0f);
    const float humidity = payloadToFloat(values.humidity_pct_x100, 100.0f);
    const float windSpeed = payloadToFloat(values.wind_speed_kmh_x100, 100.0f);
    const float rainRateValue = payloadToFloat(values.rain_rate_x10, 100.0f);
    const float rainAccumulated = payloadToFloat(values.rain_accum_x10, 10.0f);
    const float batteryV = payloadToFloat(values.battery_x10, 10.0f);
    const uint8_t batteryPct = static_cast<uint8_t>(
        constrain(lroundf((batteryV / 4.2f) * 100.0f), 0, 100)
    );

    Serial.println("[OLED->SERIAL]");
    Serial.printf("T:%.1fC H:%.0f%%\n", temperature, humidity);
    Serial.printf("Bat:%.2fV %u%%\n", batteryV, static_cast<unsigned>(batteryPct));
    Serial.printf("Wind:%.2f km/h\n", windSpeed);
    Serial.printf("Dir:%.1f deg\n", payloadToFloat(values.wind_dir_deg_x100, 100.0f));
    Serial.printf("RainR:%.2f mm/h\n", rainRateValue);
    Serial.printf("Rain24:%.2f mm\n", rainAccumulated);
}

#if DEBUG_MODE
static void powerDownOled() {
    // Mantener la pantalla en reset reduce consumo al minimo posible por software.
    pinMode(OLED_RST_PIN, OUTPUT);
    digitalWrite(OLED_RST_PIN, LOW);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.setClock(100000);

    const uint8_t addresses[] = {OLED_I2C_ADDR_PRIMARY, OLED_I2C_ADDR_SECONDARY};
    for (uint8_t addr : addresses) {
        Wire.beginTransmission(addr);
        Wire.write(0x00);  // Control byte: comandos
        Wire.write(0x8D);  // SSD1306_CHARGEPUMP
        Wire.write(0x10);  // Desactivar charge pump
        Wire.write(0xA4);  // Reanudar RAM display (evita all-pixels-on)
        Wire.write(0xAE);  // SSD1306_DISPLAYOFF
        Wire.endTransmission();
    }
}
#endif

#if DEBUG_MODE
static void restoreOledPowerInDebug() {
    if (OLED_POWER_PIN >= 0) {
        pinMode(OLED_POWER_PIN, OUTPUT);
        digitalWrite(OLED_POWER_PIN, HIGH);
    }
}

static bool i2cDevicePresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

static bool initDebugDisplay() {
    // Logica igual al proyecto que te funciona: alimentar perifericos por POWER_PIN.
    restoreOledPowerInDebug();
    delay(10);

    pinMode(OLED_RST_PIN, OUTPUT);
    digitalWrite(OLED_RST_PIN, LOW);
    delay(10);
    digitalWrite(OLED_RST_PIN, HIGH);
    delay(10);

    // Importante: no reconfigurar Wire aqui para no interferir con SHT85.
    // El bus I2C de sensores ya fue inicializado en initSensors().
    Wire.setClock(100000);
    Wire.setTimeOut(20);

    const bool hasPrimary = i2cDevicePresent(OLED_I2C_ADDR_PRIMARY);
    const bool hasSecondary = i2cDevicePresent(OLED_I2C_ADDR_SECONDARY);
    if (!hasPrimary && !hasSecondary) {
        Serial.println("[OLED] No detectada en I2C (0x3C/0x3D), se omite pantalla debug");
        return false;
    }

    // Mismo patron que tu proyecto funcional (Adafruit): 0x3C y fallback 0x3D.
    bool ok = hasPrimary && display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR_PRIMARY);
    uint8_t addr = OLED_I2C_ADDR_PRIMARY;
    if (!ok) {
        ok = hasSecondary && display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR_SECONDARY);
        addr = OLED_I2C_ADDR_SECONDARY;
    }

    if (!ok) {
        return false;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.print("Weather Station");
    display.setCursor(0, 24);
    display.print("DEBUG MODE");
    display.display();

    Serial.printf("[OLED] Inicializada en 0x%02X\n", addr);
    return true;
}
#endif

static void drawDebugDisplay(const SensorPayload& values) {
#if DEBUG_MODE
    printDebugDisplayToSerial(values);

    if (!displayReady) {
        return;
    }

    const float temperature = payloadToFloat(values.temperature_c_x100, 100.0f);
    const float humidity = payloadToFloat(values.humidity_pct_x100, 100.0f);
    const float windSpeed = payloadToFloat(values.wind_speed_kmh_x100, 100.0f);
    const float rainRateValue = payloadToFloat(values.rain_rate_x10, 100.0f);
    const float rainAccumulated = payloadToFloat(values.rain_accum_x10, 10.0f);
    const float batteryV = payloadToFloat(values.battery_x10, 10.0f);
    const uint8_t batteryPct = static_cast<uint8_t>(
        constrain(lroundf((batteryV / 4.2f) * 100.0f), 0, 100)
    );

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("T:");
    display.print(temperature, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.print("%");

    display.setCursor(0, 10);
    display.print("Bat:");
    display.print(batteryV, 2);
    display.print("V ");
    display.print(static_cast<unsigned>(batteryPct));
    display.print("%");

    display.setCursor(0, 20);
    display.print("Wind:");
    display.print(windSpeed, 2);
    display.print(" km/h");

    display.setCursor(0, 30);
    display.print("Dir:");
    display.print(payloadToFloat(values.wind_dir_deg_x100, 100.0f), 1);
    display.print(" deg");

    display.setCursor(0, 40);
    display.print("RainR:");
    display.print(rainRateValue, 2);
    display.print(" mm/h");

    display.setCursor(0, 50);
    display.print("Rain24:");
    display.print(rainAccumulated, 2);
    display.print(" mm");

    display.display();
#else
    (void)values;
#endif
}

size_t get_payload(const SensorPayload& values, uint8_t* out, size_t out_size) {
    if (out == nullptr || out_size < LORAWAN_PAYLOAD_LEN) {
        return 0;
    }

    memset(out, 0, LORAWAN_PAYLOAD_LEN);

    out[0] = static_cast<uint8_t>(values.temperature_c_x100);
    out[1] = static_cast<uint8_t>(values.temperature_c_x100 >> 8);

    out[2] = static_cast<uint8_t>(values.pressure_x100);
    out[3] = static_cast<uint8_t>(values.pressure_x100 >> 8);
    out[4] = static_cast<uint8_t>(values.pressure_x100 >> 16);
    out[5] = static_cast<uint8_t>(values.pressure_x100 >> 24);

    out[6] = static_cast<uint8_t>(values.altitude_m_x100);
    out[7] = static_cast<uint8_t>(values.altitude_m_x100 >> 8);

    out[8] = static_cast<uint8_t>(values.humidity_pct_x100);
    out[9] = static_cast<uint8_t>(values.humidity_pct_x100 >> 8);

    out[10] = static_cast<uint8_t>(values.wind_dir_deg_x100);
    out[11] = static_cast<uint8_t>(values.wind_dir_deg_x100 >> 8);
    out[12] = static_cast<uint8_t>(values.wind_dir_deg_x100 >> 16);

    out[13] = static_cast<uint8_t>(values.wind_speed_kmh_x100);
    out[14] = static_cast<uint8_t>(values.wind_speed_kmh_x100 >> 8);

    out[15] = static_cast<uint8_t>(values.rain_rate_x10);
    out[16] = static_cast<uint8_t>(values.rain_rate_x10 >> 8);
    out[17] = static_cast<uint8_t>(values.rain_rate_x10 >> 16);

    out[18] = static_cast<uint8_t>(values.rain_accum_x10);
    out[19] = static_cast<uint8_t>(values.rain_accum_x10 >> 8);
    out[20] = static_cast<uint8_t>(values.rain_accum_x10 >> 16);
    out[21] = static_cast<uint8_t>(values.rain_accum_x10 >> 24);

    out[22] = values.battery_x10;
    return LORAWAN_PAYLOAD_LEN;
}

// =========================
// LoRaWAN
// =========================
void doSend() {
    if (LMIC.opmode & OP_TXRXPEND) {
        return;
    }

    readSensors(payload, true);

    uint8_t uplink[LORAWAN_PAYLOAD_LEN] = {0};
    size_t uplinkLen = get_payload(payload, uplink, sizeof(uplink));
    if (uplinkLen != LORAWAN_PAYLOAD_LEN) {
        Serial.printf("[Payload] ERROR: tamano invalido %u (esperado %u)\n",
                      static_cast<unsigned>(uplinkLen),
                      static_cast<unsigned>(LORAWAN_PAYLOAD_LEN));
        return;
    }

    LMIC_setTxData2(
        LORA_FPORT,
        uplink,
        static_cast<uint8_t>(uplinkLen),
        0
    );

    loraTransmitStartTime = millis();
    Serial.println("[LoRa] Enviando paquete...");
}

void onEvent(ev_t ev) {
    switch (ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("[LoRa] EV_SCAN_TIMEOUT"));
            break;

        case EV_BEACON_FOUND:
            Serial.println(F("[LoRa] EV_BEACON_FOUND"));
            break;

        case EV_BEACON_MISSED:
            Serial.println(F("[LoRa] EV_BEACON_MISSED"));
            break;

        case EV_BEACON_TRACKED:
            Serial.println(F("[LoRa] EV_BEACON_TRACKED"));
            break;

        case EV_JOINING:
            Serial.println(F("[LoRa] Conectando a red..."));
            break;

        case EV_JOINED: {
            Serial.println(F("[LoRa] Conectado a red"));

            u4_t netid = 0;
            devaddr_t devaddr = 0;
            u1_t nwkKey[16];
            u1_t artKey[16];

            LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);

            Serial.print("  Network ID: ");
            Serial.println(netid, DEC);

            Serial.print("  Device Address: ");
            Serial.println(devaddr, HEX);

            Serial.print("  AppSKey: ");
            for (size_t i = 0; i < sizeof(artKey); ++i) {
                if (i != 0) Serial.print("-");
                printHex2(artKey[i]);
            }
            Serial.println();

            Serial.print("  NwkSKey: ");
            for (size_t i = 0; i < sizeof(nwkKey); ++i) {
                if (i != 0) Serial.print("-");
                printHex2(nwkKey[i]);
            }
            Serial.println();

            LMIC_setLinkCheckMode(0);
            break;
        }

        case EV_JOIN_FAILED:
            Serial.println(F("[LoRa] Error al conectar"));
            break;

        case EV_REJOIN_FAILED:
            Serial.println(F("[LoRa] Error al reconectar"));
            break;

        case EV_TXSTART:
            Serial.println(F("[LoRa] Transmitiendo..."));
            break;

        case EV_TXCOMPLETE: {
            uint32_t elapsed = millis() - loraTransmitStartTime;
            Serial.println(F("[LoRa] Transmision completada"));
            Serial.print(F("  Tiempo: "));
            Serial.print(elapsed);
            Serial.println(F(" ms"));

            if (LMIC.txrxFlags & TXRX_ACK) {
                Serial.println(F("  ACK recibido"));
            }

            if (LMIC.dataLen) {
                Serial.print(F("  Datos recibidos: "));
                Serial.print(LMIC.dataLen);
                Serial.println(F(" bytes"));
            }

            if (ENABLE_DEEP_SLEEP && !DEBUG_MODE) {
                Serial.println(F("[Sistema] Durmiendo..."));
            } else {
                Serial.println(F("[Sistema] Deep sleep deshabilitado"));
            }
            Serial.println();

            delay(100);
            if (ENABLE_DEEP_SLEEP && !DEBUG_MODE) {
                sleepMillis(WAKE_TIME_MS);
            }
            break;
        }

        case EV_TXCANCELED:
            Serial.println(F("[LoRa] Transmision cancelada"));
            break;

        case EV_RXCOMPLETE:
            Serial.println(F("[LoRa] EV_RXCOMPLETE"));
            break;

        case EV_RXSTART:
            break;

        case EV_JOIN_TXCOMPLETE:
            Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
            break;

        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;

        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;

        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;

        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;

        default:
            Serial.print(F("Unknown event: "));
            Serial.println(static_cast<unsigned>(ev));
            break;
    }
}

// =========================
// Setup / Loop
// =========================
void setup() {
    Serial.begin(115200);
    delay(1000);

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    const bool wokeFromDeepSleep =
        (wakeCause == ESP_SLEEP_WAKEUP_TIMER) ||
        (wakeCause == ESP_SLEEP_WAKEUP_EXT0);

    Serial.println();
    Serial.println("=================================");
    Serial.println("LILYGO T3 v1.6.1 - Weather LoRa");
    Serial.println("=================================");
    const esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("[Sistema] DEBUG_MODE=%s\n", DEBUG_MODE_ENABLED ? "true" : "false");
    Serial.printf("[Sistema] Reset reason=%d\n", static_cast<int>(resetReason));

    initSensors();
    Serial.println("[Setup] Sensores inicializados");

    if (DEBUG_MODE_ENABLED) {
#if DEBUG_MODE
        // Failsafe: si el arranque anterior cayo por INT_WDT (5),
        // desactivar OLED en este boot para romper bucles de reinicio en debug.
        if (resetReason == ESP_RST_INT_WDT) {
            displayReady = false;
            Serial.println("[OLED] Failsafe activo: se omite OLED por reset INT_WDT previo");
        } else {
            Serial.println("[Setup] Iniciando OLED debug...");
            displayReady = initDebugDisplay();
            if (!displayReady) {
                Serial.println("[OLED] ERROR init en 0x3C/0x3D");
            }
        }
        Serial.println("[Setup] OLED debug procesada");
#endif
    }

    if (!DEBUG_MODE_ENABLED && ENABLE_RAIN_SENSOR && wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[Rain] Wakeup por pluviometro");
        rain();

        if (ENABLE_DEEP_SLEEP) {
            uint64_t pendingMs = getPendingSleepMs();
            if (pendingMs > 100) {
                Serial.printf("[Rain] Guardado contador. Volviendo a dormir (%llu ms restantes)\n", pendingMs);
                sleepMillis(pendingMs);
            }
        }
    }

    if (!DEBUG_MODE_ENABLED && ENABLE_LORA) {
        os_init();
        LMIC_reset();
        LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

        Serial.println("[LoRa] Inicializado");
        Serial.println("[LoRa] Esperando conexion a red...");
    } else {
        Serial.println(DEBUG_MODE_ENABLED ? "[LoRa] Deshabilitado por DEBUG_MODE" : "[LoRa] Deshabilitado");
    }

    Serial.println();

    if (DEBUG_MODE_ENABLED) {
        lastMeasure = millis() - DEBUG_REFRESH_MS;
    } else {
        lastMeasure = wokeFromDeepSleep
            ? (millis() - LOOP_TIME_MS)
            : millis();
    }
}

void loop() {
    if (DEBUG_MODE_ENABLED) {
        bool rainEvent = false;
        if (ENABLE_RAIN_SENSOR) {
            rainEvent = (processRainInterrupts() > 0);
        }

        if (rainEvent) {
            readSensors(payload, true);
#if DEBUG_MODE
            restoreOledPowerInDebug();
#endif
            drawDebugDisplay(payload);
            lastMeasure = millis();
        }

        unsigned long now = millis();
        if ((now - lastMeasure) >= DEBUG_REFRESH_MS) {
            lastMeasure = now;
            readSensors(payload, true);
#if DEBUG_MODE
            restoreOledPowerInDebug();
#endif
            drawDebugDisplay(payload);
        }

        delay(10);
        return;
    }

    if (ENABLE_LORA) {
        os_runloop_once();
    }

    bool rainEvent = false;
    if (ENABLE_RAIN_SENSOR) {
        rainEvent = (processRainInterrupts() > 0);
    }

    if (rainEvent) {
        readSensors(payload, true);
        lastMeasure = millis();
    }

    unsigned long now = millis();
    if ((now - lastMeasure) > LOOP_TIME_MS) {
        lastMeasure = now;
        if (ENABLE_LORA) {
            doSend();
        } else {
            readSensors(payload, true);
        }
    }
}
