# IOT Weather Station (LILYGO T3 + LoRaWAN)

Firmware de estación meteorológica para ESP32 (LILYGO T3 / TTGO LoRa32 v1) con envío por LoRaWAN (OTAA), lectura de sensores ambientales y ciclo de bajo consumo con deep sleep.

## Resumen funcional

1. Inicializa sensores y radio LoRaWAN.
2. Lee temperatura y humedad desde SHT85.
3. Lee veleta (dirección) y anemómetro (velocidad) mediante GPIO/ADC + interrupciones.
4. Lee pluviómetro por interrupción y mantiene lluvia acumulada diaria en memoria RTC.
5. Empaqueta los datos en un payload binario de 23 bytes.
6. Envía por LoRaWAN en FPort 1.
7. Entra en deep sleep y puede despertarse por temporizador o por evento de lluvia.

## Hardware y entorno

- MCU/placa: ESP32, entorno PlatformIO `ttgo-lora32-v1`.
- Framework: Arduino.
- Región LoRaWAN: EU868.
- Radio LMIC: SX1276.
- Sensor T/H: SHT85 (I2C).
- Sensores meteorológicos por pulsos/analógico: anemómetro, pluviómetro, veleta.

Configuración relevante en [platformio.ini](platformio.ini):

- `platform = espressif32`
- `board = ttgo-lora32-v1`
- `CFG_eu868=1`
- `CFG_sx1276_radio=1`

## Estructura del proyecto

- [src/main.cpp](src/main.cpp): ciclo principal, estado LoRaWAN, creación del payload y envío.
- [src/sensores.cpp](src/sensores.cpp): inicialización y lectura de sensores, interrupciones de viento/lluvia.
- [src/sleep_utils.cpp](src/sleep_utils.cpp): deep sleep, wakeup por temporizador y lluvia.
- [include/config.h](include/config.h): constantes, flags de sensores, pines y definición del payload.
- [include/credentials.h](include/credentials.h): claves OTAA (AppEUI, DevEUI, AppKey).
- [cambios.md](cambios.md): histórico de cambios/correcciones recientes.

## Pines usados

### LoRa (LMIC)

Definidos en [src/main.cpp](src/main.cpp#L11):

- `NSS`: GPIO 18
- `RST`: GPIO 23
- `DIO0/DIO1/DIO2`: GPIO 26 / 33 / 32

### Sensores

Definidos en [include/config.h](include/config.h#L32):

- `BATTERY_PIN`: GPIO 12
- `VOLTAGE_PIN`: GPIO 15 (habilitación de electrónica para viento/veleta)
- `WIND_VANE_PIN`: GPIO 4 (ADC)
- `WIND_SENSOR_PIN`: GPIO 34 (anemómetro, interrupción)
- `RAIN_SENSOR_PIN`: GPIO 36 (pluviómetro, interrupción y wakeup)

Notas eléctricas implementadas en código:

- GPIO 34 y 36 no tienen pull-up/pull-down interno.
- Para lluvia se asume pull-down externo y pulso a HIGH al cerrar contacto.

## Flujo de ejecución del firmware

### 1) Arranque

En [src/main.cpp](src/main.cpp#L258):

- Inicia Serial a 115200.
- Detecta causa de wakeup.
- Llama a `initSensors()`.

### 2) Caso especial: wakeup por lluvia

En [src/main.cpp](src/main.cpp#L271):

- Si despertó por `ESP_SLEEP_WAKEUP_EXT0`, incrementa contador de lluvia con `rain()`.
- Recupera tiempo pendiente de sueño con `getPendingSleepMs()`.
- Si todavía falta ventana de sueño, vuelve a dormir sin transmitir.

Esto evita romper el ciclo normal de envío por cada tip del pluviómetro.

### 3) Inicialización LoRaWAN

En [src/main.cpp](src/main.cpp#L284):

- `os_init()`, `LMIC_reset()` y ajuste de error de reloj.
- Credenciales OTAA cargadas desde [include/credentials.h](include/credentials.h#L7).

### 4) Bucle principal

En [src/main.cpp](src/main.cpp#L301):

- Ejecuta `os_runloop_once()` para LMIC.
- Cada `LOOP_TIME_MS` (20 s) llama a `doSend()`.

### 5) Envío y post-envío

En [src/main.cpp](src/main.cpp#L84) y [src/main.cpp](src/main.cpp#L184):

- `doSend()` lee sensores, arma payload y encola uplink (`LMIC_setTxData2`, no confirmado).
- Tras `EV_TXCOMPLETE`, entra en deep sleep `WAKE_TIME_MS` (5 min) si está habilitado.

## Lógica de sensores

### SHT85 (temperatura/humedad)

En [src/sensores.cpp](src/sensores.cpp#L127):

- Inicializa I2C y valida presencia del SHT85.
- Si falla, reinicia el equipo.

### Viento

En [src/sensores.cpp](src/sensores.cpp#L109), [src/sensores.cpp](src/sensores.cpp#L158) y [src/sensores.cpp](src/sensores.cpp#L197):

- Anemómetro por interrupción `FALLING` con debounce de 15 ms.
- Veleta por ADC (12 bits), promedio de 8 muestras y mapeo 0..360°.
- Velocidad calculada desde rotaciones acumuladas entre lecturas.

### Lluvia

En [src/sensores.cpp](src/sensores.cpp#L120), [src/sensores.cpp](src/sensores.cpp#L222) y [src/sensores.cpp](src/sensores.cpp#L239):

- Pluviómetro por interrupción `RISING`.
- Ignora rebotes/eventos cercanos (< 2 s).
- `rainRate` se estima por intervalo entre tips.
- Acumulado diario en memoria RTC (`RTC_DATA_ATTR`) con ventana móvil de 24 h.

### Batería

En [src/sensores.cpp](src/sensores.cpp#L284):

- Lectura analógica y escalado a voltaje estimado.
- Se calcula porcentaje para mostrar por serie.
- En payload se mantiene formato legado de `battery_x10`.

## Deep sleep y wakeup

En [src/sleep_utils.cpp](src/sleep_utils.cpp#L9):

- Siempre configura wakeup por temporizador.
- Si lluvia está habilitada, configura `ext0` en GPIO36 a nivel HIGH, pero solo si el pin está en LOW antes de dormir (protección anti-bucle).
- Guarda un deadline RTC para poder retomar el sueño restante tras un wakeup por lluvia.

## Payload LoRaWAN (23 bytes)

Definición en [include/config.h](include/config.h#L60) y empaquetado en [src/main.cpp](src/main.cpp#L40).

| Bytes | Campo | Escala esperada |
|---|---|---|
| 0..1 | `temperature_c_x100` | °C x100 |
| 2..5 | `pressure_x100` | hPa x100 |
| 6..7 | `altitude_m_x100` | m x100 |
| 8..9 | `humidity_pct_x100` | % x100 |
| 10..12 | `wind_dir_deg_x100` | grados x100 (24 bits) |
| 13..14 | `wind_speed_kmh_x100` | km/h x100 |
| 15..17 | `rain_rate_x10` | lluvia instantánea (24 bits) |
| 18..21 | `rain_accum_x10` | lluvia acumulada 24 h x10 |
| 22 | `battery_x10` | valor legado |

Importante:

- Los campos de 3 bytes (24 bits) requieren sign-extension al decodificar si se manejan como enteros con signo.
- En el código actual, `rain_rate_x10` se calcula con factor x100 en lugar de x10 ([src/sensores.cpp](src/sensores.cpp#L361)). Ajustar decoder según lo que se quiera conservar (nombre o escala real).

## Configuración principal

### Flags de comportamiento

En [include/config.h](include/config.h#L12):

- `LOOP_TIME_MS = 20000`
- `WAKE_TIME_MS = 5 * 60 * 1000`
- `ENABLE_DEEP_SLEEP = true`
- `ENABLE_LORA = true`

### Flags de sensores

En [include/config.h](include/config.h#L21):

- SHT85, batería, anemómetro, pluviómetro y veleta habilitados.
- Presión y altitud también habilitados por flag, pero en implementación actual se envían a 0.

## Build, carga y monitor serie

Requisitos:

1. VS Code + extensión PlatformIO.
2. Toolchain de ESP32 instalada por PlatformIO.
3. Placa conectada por USB.

Comandos típicos (si `pio` está disponible en terminal):

```bash
pio run -e lilygo-t3
pio run -e lilygo-t3 -t upload
pio device monitor -b 115200
```

También se puede compilar/subir desde los botones de PlatformIO en VS Code.

## Seguridad de credenciales

Actualmente [include/credentials.h](include/credentials.h) contiene claves OTAA reales en texto plano. Para entornos compartidos:

- Rotar claves si este repositorio fue público.
- Excluir credenciales del control de versiones.
- Usar plantillas (por ejemplo `credentials.example.h`) y archivo local ignorado.

## Limitaciones conocidas

1. `pressure` y `altitude` están en 0.0 por no haber sensor implementado aún ([src/sensores.cpp](src/sensores.cpp#L319)).
2. Escala de `rain_rate_x10` no coincide con su nombre (x100 en implementación actual).
3. El cálculo de velocidad de viento depende del número de rotaciones entre lecturas y de factores empíricos; puede requerir calibración de campo.

## Historial de cambios

Resumen detallado de modificaciones recientes en [cambios.md](cambios.md).
