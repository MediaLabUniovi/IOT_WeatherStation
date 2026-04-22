# Cambios y correcciones realizadas

Fecha: 17 de abril de 2026

## 1) Refactor de sensores

Se separo la logica de sensores en un modulo dedicado:

- Se creo src/sensores.cpp con la inicializacion y lectura de sensores.
- Se movieron a ese modulo las funciones de viento y lluvia.
- Se agrego initSensors() para concentrar toda la configuracion de pines/sensores.
- main.cpp quedo enfocado en LoRaWAN, ciclo y envio de payload.

## 2) Viento y veleta

Integraciones y ajustes:

- Se sustituyeron las funciones de viento/lluvia por las versiones solicitadas, adaptadas al proyecto.
- Se mejoro la lectura de veleta con:
  - configuracion ADC explicita en el pin de veleta,
  - promedio de muestras,
  - mapeo y acotado del valor a 0..360.
- Se agrego trazado en serial del valor bruto de la veleta para diagnostico.
- Se reforzo que VOLTAGE_PIN (GPIO15) este en HIGH justo antes de leer la veleta, con un pequeno tiempo de estabilizacion.

## 3) Lluvia (interrupciones y acumulado)

Cambios principales:

- Se corrigio la logica para hardware con pull-down externo y pulso a VCC:
  - interrupcion de lluvia en flanco RISING.
- Se ajusto el aviso/comentarios de cableado para lluvia a pull-down externo.
- Se corrigio el filtro inicial para no perder el primer tip tras arranque/despertar.
- Se agrego contador acumulado persistente en memoria RTC:
  - acumulado por tips,
  - conversion a mm acumulados.
- Se cambio el comportamiento del acumulado para que represente lluvia acumulada en ventana diaria de 24h (no acumulado infinito historico).

## 4) Deep sleep y wakeup por lluvia

Se implemento compatibilidad de wakeup por pluviómetro sin romper el ciclo de envio:

- sleep_utils.cpp ahora configura wakeup por timer y por lluvia (ext0).
- Con hardware de lluvia activo en HIGH (pull-down externo), el wakeup por lluvia se arma por nivel alto en ext0.
- Se agrego un deadline de sueno en RTC para conservar la ventana del ciclo original.
- Al despertar por lluvia:
  - se registra el tip,
  - se vuelve a dormir con el tiempo restante,
  - se evita enviar fuera de ciclo.
- Se anadio proteccion anti-bucle de wakeup:
  - solo se arma wake por lluvia si el pin esta en estado de reposo esperado (LOW con pull-down),
  - si el pin esta en HIGH al dormir, se desactiva wake por lluvia en ese ciclo para evitar despertares continuos.

## 5) Payload LoRaWAN

Se amplio la estructura del payload para incluir lluvia acumulada:

- Longitud total: 23 bytes.
- Nuevo campo: rain_accum_x10 (mm acumulados x10).
- El campo rain_accum_x10 ahora corresponde a lluvia acumulada diaria (ventana de 24h).
- Reubicacion de bateria al ultimo byte.

Mapa final de bytes:

- 0..1 temperatura x100
- 2..5 presion x100
- 6..7 altitud x100
- 8..9 humedad x100
- 10..12 direccion viento x100 (3 bytes LSB)
- 13..14 velocidad viento x100
- 15..17 rain rate
- 18..21 lluvia acumulada x10
- 22 bateria (% 0..100)

## 8) Regla de reinicio del acumulado de lluvia

Se implemento un reinicio automatico del acumulado de lluvia cada 24 horas usando tiempo RTC del chip:

- Se guarda la marca de inicio de ventana diaria en memoria RTC.
- Antes de contar tips y antes de leer el acumulado se valida si la ventana de 24h vencio.
- Al vencer, el acumulado se reinicia y comienza una nueva ventana diaria.

## 6) Configuracion de compilacion (LMIC)

Se elimino el warning de radio no definido en LMIC:

- Se agrego en platformio.ini: -D CFG_sx1276_radio=1

## 7) Decoder de payload (JS)

Se actualizo el decoder para el nuevo formato de 23 bytes:

- corrigiendo desplazamientos de 32 bits,
- eliminando el campo LLUVIA legado,
- agregando rainAccumulated.

## Archivos modificados

- platformio.ini
- include/config.h
- src/main.cpp
- src/sensores.cpp
- src/sleep_utils.cpp
