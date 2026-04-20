#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>

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

    readSensors(payload);

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

    Serial.println();
    Serial.println("╔════════════════════════════════╗");
    Serial.println("║ 📡 ENVIANDO A RED LoRaWAN...   ║");
    Serial.println("╚════════════════════════════════╝");
}

void onEvent(ev_t ev) {
    switch (ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("[LoRa] ❌ EV_SCAN_TIMEOUT"));
            break;

        case EV_BEACON_FOUND:
            Serial.println(F("[LoRa] ✓ EV_BEACON_FOUND"));
            break;

        case EV_BEACON_MISSED:
            Serial.println(F("[LoRa] ⚠ EV_BEACON_MISSED"));
            break;

        case EV_BEACON_TRACKED:
            Serial.println(F("[LoRa] ✓ EV_BEACON_TRACKED"));
            break;

        case EV_JOINING:
            Serial.println(F("[LoRa] ⏳ Conectando a red..."));
            break;

        case EV_JOINED: {
            Serial.println(F("[LoRa] ✓ Conectado a red"));

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
            Serial.println(F("[LoRa] ❌ Error al conectar"));
            break;

        case EV_REJOIN_FAILED:
            Serial.println(F("[LoRa] ❌ Error al reconectar"));
            break;

        case EV_TXSTART:
            Serial.println(F("[LoRa] 📡 Transmitiendo..."));
            break;

        case EV_TXCOMPLETE:
            {
                uint32_t elapsed = millis() - loraTransmitStartTime;
                Serial.println(F("[LoRa] ✓ Transmisión completada"));
                Serial.print(F("  Tiempo: "));
                Serial.print(elapsed);
                Serial.println(F(" ms"));

                if (LMIC.txrxFlags & TXRX_ACK) {
                    Serial.println(F("  ✓ ACK recibido"));
                }

                if (LMIC.dataLen) {
                    Serial.print(F("  ↓ Datos recibidos: "));
                    Serial.print(LMIC.dataLen);
                    Serial.println(F(" bytes"));
                }

                if (ENABLE_DEEP_SLEEP) {
                    Serial.println(F("[Sistema] Durmiendo..."));
                } else {
                    Serial.println(F("[Sistema] Deep sleep deshabilitado"));
                }
                Serial.println();
            }
            delay(100);
            if (ENABLE_DEEP_SLEEP) {
                sleepMillis(WAKE_TIME_MS);
            }
            break;

        case EV_TXCANCELED:
            Serial.println(F("[LoRa] ❌ Transmisión cancelada"));
            break;

        case EV_RXCOMPLETE:
            Serial.println(F("[LoRa] ✓ EV_RXCOMPLETE"));
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

    initSensors();

    if (ENABLE_RAIN_SENSOR && wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[Rain] Wakeup por pluviómetro");
        rain();

        if (ENABLE_DEEP_SLEEP) {
            uint64_t pendingMs = getPendingSleepMs();
            if (pendingMs > 100) {
                Serial.printf("[Rain] Guardado contador. Volviendo a dormir (%llu ms restantes)\n", pendingMs);
                sleepMillis(pendingMs);
            }
        }
    }

    if (ENABLE_LORA) {
        os_init();
        LMIC_reset();
        LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

        Serial.println("[LoRa] ✓ Inicializado");
        Serial.println("[LoRa] ⏳ Esperando conexión a red...");
    } else {
        Serial.println("[LoRa] ⚠ Deshabilitado");
    }
    
    Serial.println();

    lastMeasure = wokeFromDeepSleep
        ? (millis() - LOOP_TIME_MS)
        : millis();
}

void loop() {
    if (ENABLE_LORA) {
        os_runloop_once();
    }

    if (ENABLE_RAIN_SENSOR) {
        processRainInterrupts();
    }

    unsigned long now = millis();
    if ((now - lastMeasure) > LOOP_TIME_MS) {
        lastMeasure = now;
        if (ENABLE_LORA) {
            doSend();
        } else {
            readSensors(payload);
        }
    }
}
