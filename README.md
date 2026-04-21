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
8. Incluye un modo debug para pruebas sin envíos LoRa ni deep sleep.

## Hardware y entorno

- MCU/placa: ESP32, entorno PlatformIO `ttgo-lora32-v1`
- Framework: Arduino
- Región LoRaWAN: EU868
- Radio LMIC: SX1276
- Sensor T/H: SHT85 (I2C)
- Sensores meteorológicos: anemómetro, pluviómetro y veleta

Configuración relevante en platformio.ini:

- platform = espressif32
- board = ttgo-lora32-v1
- CFG_eu868=1
- CFG_sx1276_radio=1

## Estructura del proyecto

- src/main.cpp: ciclo principal, gestión de modos, LoRaWAN y envío
- src/sensores.cpp: lectura de sensores e interrupciones
- src/sleep_utils.cpp: deep sleep y wakeup
- include/config.h: configuración general y payload
- include/credentials.h: claves OTAA
- cambios.md: historial de cambios

## Pines usados

### LoRa (LMIC)

- NSS: GPIO 18
- RST: GPIO 23
- DIO0/DIO1/DIO2: GPIO 26 / 33 / 32

### Sensores

- BATTERY_PIN: GPIO 12
- VOLTAGE_PIN: GPIO 15
- WIND_VANE_PIN: GPIO 4 (ADC)
- WIND_SENSOR_PIN: GPIO 34 (interrupción)
- RAIN_SENSOR_PIN: GPIO 36 (interrupción y wakeup)

Notas:
- GPIO 34 y 36 no tienen pull interno
- El pluviómetro usa pull-down externo y genera pulso HIGH

## Modos de funcionamiento

### Modo normal

Modo de uso una vez la estación esté instalada:

- lectura de sensores
- envío por LoRaWAN
- deep sleep para ahorro de batería
- wakeup por temporizador o lluvia

### Modo debug

Modo de pruebas:

- sin envíos LoRa
- sin deep sleep
- ejecución continua para validar sensores

## Flujo de ejecución

### Arranque

- inicia Serial
- detecta causa de wakeup
- inicializa sensores
- selecciona modo debug o normal

### Wakeup por lluvia

- si despierta por pluviómetro:
  - registra evento de lluvia
  - recupera tiempo restante de sueño
  - vuelve a dormir si corresponde

### Inicialización LoRa

- os_init()
- LMIC_reset()
- carga credenciales OTAA

### Loop

Modo normal:
- ejecuta LMIC
- procesa lluvia
- cada LOOP_TIME_MS envía datos

Modo debug:
- lectura continua
- sin sleep ni envío

### Envío

- lectura de sensores
- generación de payload
- envío con LMIC_setTxData2

Tras envío:
- entra en deep sleep si está habilitado

## Lógica de sensores

### Temperatura y humedad

- lectura desde SHT85

### Viento

- anemómetro por interrupciones
- veleta por ADC
- velocidad calculada por pulsos

Se ha ajustado el cálculo para evitar valores fijos (antes se quedaba en ~1.4 km/h).

### Lluvia

- pluviómetro por interrupción
- acumulado en memoria RTC
- cálculo en función del tiempo entre eventos

Se ha mejorado el cálculo para obtener valores más lineales. Antes tendía a dar valores fijos (~1.2 mm/h) al detectar agua.

### Batería

- lectura analógica
- conversión a voltaje

Se ha corregido un desfase de aproximadamente 0.6V para obtener valores más reales.

## Deep sleep

- wakeup por temporizador
- wakeup por lluvia (GPIO36)
- control para evitar bucles de wakeup
- recuperación de tiempo restante tras evento

## Payload LoRaWAN (23 bytes)

Formato:

- 0..1: temperatura (°C x100)
- 2..5: presión (hPa x100)
- 6..7: altitud (m x100)
- 8..9: humedad (% x100)
- 10..12: dirección viento (grados x100)
- 13..14: velocidad viento (km/h x100)
- 15..17: lluvia instantánea
- 18..21: lluvia acumulada
- 22: batería

Importante:

- El payload NO se ha modificado
- Misma estructura y tamaño
- Compatible con SensorLab

## Configuración

- LOOP_TIME_MS = 20000
- WAKE_TIME_MS = 5 minutos
- ENABLE_DEEP_SLEEP = true
- ENABLE_LORA = true

Modo debug:
- DEBUG_MODE = 1 (activo)
- DEBUG_MODE = 0 (normal)

## Build y carga

pio run -e lilygo-t3
pio run -e lilygo-t3 -t upload
pio device monitor -b 115200

## Estado actual

- estación funcionando correctamente
- viento y lluvia ajustados tras pruebas reales
- batería corregida
- payload compatible
- pendiente de instalación

## Limitaciones conocidas

- presión y altitud no implementadas (pueden ir a 0)
- calibración de viento puede requerir ajuste en campo
- cálculo de lluvia puede necesitar ajuste fino tras instalación

## Seguridad

- credenciales OTAA en texto plano
- recomendable excluir del repo en producción

## Historial de cambios

Ver cambios.md