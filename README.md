# CarritoRC

Controlador de coche RC basado en ESP32 que crea su propia red WiFi y sirve una interfaz web móvil para manejarlo en tiempo real.

## Tabla de contenido

- [Componentes](#componentes)
- [Conexiones físicas](#conexiones-físicas)
- [Instalación / build](#instalación--build)
- [Configuración inicial](#configuración-inicial)
- [Interfaz (API HTTP)](#interfaz-api-http)
- [Indicadores de estado](#indicadores-de-estado)
- [Comportamiento ante fallos y casos borde](#comportamiento-ante-fallos-y-casos-borde)
- [Tareas automáticas de fondo](#tareas-automáticas-de-fondo)
- [Solución de problemas](#solución-de-problemas)

## Componentes

| Componente | Descripción |
|---|---|
| Placa ESP32 (`board = esp32dev`) | Microcontrolador principal. Corre el firmware, el punto de acceso WiFi y el servidor web. |
| Puente H para motor DC¹ | Controlado con 2 pines de dirección (`In2`, `In3`) + 1 pin PWM (`ENB`). |
| Servo de dirección | Servo estándar de hobby, PWM a 50 Hz, pulso 500–2400 µs. |
| Servo auxiliar | Mismo tipo de servo; puede operar en modo manual o en oscilación automática continua. |
| Dispositivo cliente (móvil/tablet con navegador) | Se conecta a la red WiFi del ESP32 y controla el coche desde la página web servida por el propio ESP32. |

¹ El código no nombra el chip del puente H en ningún comentario; la nomenclatura de pines (`In2`/`In3`/`ENB`) es la convención habitual de módulos tipo L298N, pero esto es una inferencia y no un dato verificado en el código fuente.

## Conexiones físicas

El firmware usa tres subsistemas de hardware totalmente independientes entre sí: ninguno comparte pines, canal PWM ni temporizador con otro.

### Motor DC (vía puente H)

| Señal | GPIO ESP32 | Notas |
|---|---|---|
| `In2` | 25 | Pin de dirección 1. |
| `In3` | 33 | Pin de dirección 2. |
| `ENB` | 32 | PWM de velocidad. Canal LEDC 0, 10 kHz, resolución de 8 bits (`ledcSetup(0, 10000, 8)`). |

`In2`/`In3` ambos en LOW detiene el motor. El canal LEDC 0 está reservado exclusivamente para este pin; ningún servo lo usa.

### Servo de dirección

| Señal | GPIO ESP32 | Notas |
|---|---|---|
| `servoPin` | 13 | PWM a 50 Hz, pulso 500–2400 µs, gestionado por la librería ESP32Servo. |

### Servo auxiliar

| Señal | GPIO ESP32 | Notas |
|---|---|---|
| `servoAuxPin` | 27 | Mismo rango de PWM que el servo de dirección, pero con su propio ciclo de movimiento e independiente del servo de dirección. |

Los timers 2 y 3 del hardware PWM del ESP32 se reservan por adelantado (`ESP32PWM::allocateTimer(2)` y `(3)`) como grupo para que la librería ESP32Servo los use según necesite; el código no asigna un timer fijo a cada servo individualmente.

### Red WiFi (punto de acceso)

| Parámetro | Valor |
|---|---|
| Modo | Punto de acceso únicamente (`WIFI_AP`), sin conexión a internet ni a otra red. |
| SSID | `COCHERC_B` |
| Contraseña | `87654321` |
| Canal | 2 |
| IP fija del ESP32 | `192.168.4.5` |
| Gateway | `192.168.4.1` |
| Máscara de subred | `255.255.255.0` |
| Clientes máximos simultáneos | 1 |

El radio WiFi no comparte GPIOs con el motor ni con los servos, por lo que un problema de conexión nunca es causado por el cableado del motor/servos y viceversa.

## Instalación / build

### Requisitos

- [PlatformIO](https://platformio.org/) (CLI o extensión de VS Code).
- Placa ESP32 compatible con la definición `esp32dev`.

### Dependencias declaradas (`platformio.ini`)

| Dependencia | Versión declarada | Fuente | Instalada actualmente |
|---|---|---|---|
| `madhephaestus/ESP32Servo` | `^0.13.0` | PlatformIO Registry | `0.13.0` (verificado en `.pio/libdeps/esp32dev/ESP32Servo/library.properties`) |

`WiFi` y `WebServer` no aparecen en `lib_deps` porque forman parte del framework Arduino-ESP32 (`framework = arduino`), no son dependencias externas.

### Configuración de plataforma (`platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    madhephaestus/ESP32Servo @ ^0.13.0
```

No hay flags de compilación adicionales (`build_flags`) definidos en `platformio.ini`; el proyecto se construye con la configuración por defecto de PlatformIO para `esp32dev`.

### Pasos

```bash
# Compilar
pio run

# Compilar y flashear al ESP32
pio run --target upload

# Abrir monitor serie (115200 baudios)
pio device monitor

# Compilar, flashear y luego abrir el monitor
pio run --target upload; pio device monitor

# Limpiar artefactos de compilación
pio run --target clean
```

## Configuración inicial

1. Conectar el cableado según la sección [Conexiones físicas](#conexiones-físicas).
2. Flashear el firmware (`pio run --target upload`).
3. Al arrancar, el ESP32 crea la red WiFi `COCHERC_B` (contraseña `87654321`, canal 2). Estos valores son constantes fijas en el código (`ssid`, `password`, `canal` en `src/main.cpp`) — para cambiarlos hay que editar el código fuente y volver a flashear, no hay configuración en tiempo de ejecución.
4. Conectar el dispositivo de control (móvil/tablet) a esa red WiFi. Solo se admite **1 cliente conectado a la vez**; si otro dispositivo intenta unirse mientras hay uno conectado, no podrá asociarse a la red.
5. Abrir un navegador en `http://192.168.4.5/` (IP fija; no depende de DHCP).
6. La interfaz carga con: dirección centrada (90°), servo auxiliar centrado (90°) en modo manual, motor detenido y sentido "Adelante" preseleccionado.

## Interfaz (API HTTP)

El servidor web escucha en el puerto 80. Todas las rutas son `GET` con parámetros de query. Esta tabla fue reconstruida leyendo directamente `configurarRutas()` en `src/main.cpp`; una versión anterior de esta documentación solo listaba 4 rutas (`/command`, `/velocidad`, `/direccion`, `/servoaux`) y omitía `/motor`, `/servoauxmodo` y la ruta raíz — se corrige aquí.

| Ruta | Parámetros | Descripción |
|---|---|---|
| `/` | — | Sirve la página de control HTML (embebida en flash). Responde con cabecera `Cache-Control: no-cache, no-store, must-revalidate`. |
| `/command` | `cmd=avanzar\|reversa\|parar` | Fija el sentido del motor (o lo detiene) usando la velocidad ya almacenada en el servidor. **No recibe velocidad como parámetro**: si nunca se llamó antes a `/velocidad`, el motor queda en marcha con PWM en 0 (pines activados pero sin girar). |
| `/velocidad` | `value` (entero) | Fija la velocidad (duty PWM) del motor. Se redondea al múltiplo de 5 más cercano y se acota al rango 85–230. Solo escribe el PWM real si el motor no está en modo "parar" (ver nota abajo). |
| `/direccion` | `value` (entero/decimal) | Fija el ángulo objetivo del servo de dirección. Se acota al rango 20–160. **A diferencia de `/velocidad` y `/servoaux`, este endpoint NO redondea a múltiplos de 5** — acepta cualquier grado entero dentro del rango (ver nota en [CODIGO.md](CODIGO.md#decisiones-de-diseño-no-obvias)). |
| `/servoaux` | `value` (entero) | Fija el ángulo objetivo del servo auxiliar. Se redondea al múltiplo de 5 más cercano y se acota a 20–160. Se ignora silenciosamente (responde `OK` sin aplicar nada) si el servo auxiliar está en modo oscilación automática. |
| `/servoauxmodo` | `enabled=0\|1` | Cambia el modo del servo auxiliar. `enabled=1` → control manual (responde a `/servoaux`). `enabled=0` → oscilación automática continua (barrido 0°–180°, ignora `/servoaux`). |
| `/motor` | `value=-255..255` | Endpoint combinado de sentido + velocidad: signo = sentido (negativo = reversa, positivo = adelante, 0 = parar), magnitud = velocidad PWM aplicada directamente. Es el endpoint que usa la interfaz web incluida para mover el motor, en vez de `/command`+`/velocidad` (ver nota abajo). |
| *(cualquier otra ruta)* | — | `404 text/plain "Not found"`. |

Todos los endpoints devuelven `200 text/plain "OK"` en éxito. Si falta un parámetro requerido devuelven `400` con un mensaje descriptivo (`"Falta cmd"`, `"Falta value"`, `"Falta enabled"`) y `/command` devuelve `400 "Comando invalido"` ante un `cmd` no reconocido.

**Nota importante sobre `/command` y `/velocidad`:** la interfaz web embebida en el firmware **no llama nunca** a estas dos rutas; los botones de sentido y el slider de velocidad usan exclusivamente `/motor`. Ambas rutas siguen activas y funcionales en el backend (útiles para scripts externos o pruebas manuales vía `curl`), pero son legado respecto al flujo que usa la interfaz actual.

**Límites de tasa (rate limit) del backend**, independientes del throttle de 80 ms del frontend:

| Ruta(s) | Intervalo mínimo | Bypass |
|---|---|---|
| `/direccion` | 25 ms | Se salta el límite cuando `value=90` (recentrado), para que el auto-centrado al soltar el slider nunca se retrase. |
| `/servoaux` | 25 ms | Sin bypass. |
| `/velocidad` y `/motor` | 40 ms (comparten el mismo contador interno) | `/motor` se salta el límite cuando `value=0` (parada), para que detener el motor nunca se retrase. |

## Indicadores de estado

El firmware **no controla ningún LED físico ni escribe logs por el puerto serie** (`Serial.begin(115200)` se usa únicamente para los mensajes de arranque del propio framework; no hay ninguna llamada a `Serial.print`/`Serial.println` en el código de la aplicación). El único canal de retroalimentación es visual, dentro de la propia interfaz web:

| Elemento visual | Significado |
|---|---|
| Botón "Adelante"/"Reversa" resaltado (clase `on`) | Sentido de marcha actualmente seleccionado. |
| Número junto a "Velocidad" | Valor PWM (0 o 85–230) enviado en el último comando de motor. |
| Número junto a "Direccion" | Último ángulo enviado para el servo de dirección. |
| Número junto a "Giro automatico" / texto `AUTO` | Ángulo manual del servo auxiliar, o literal `AUTO` cuando está en modo oscilación automática. |
| Slider de dirección atenuado (opacidad reducida, no interactivo) | Se activa cuando el servo auxiliar está en modo automático — el control manual queda bloqueado mientras tanto. |

No existe ninguna otra señal de estado (sin buzzer, sin LED de conexión, sin log accesible remotamente).

## Comportamiento ante fallos y casos borde

- **Reinicio / corte de energía:** todo el estado vive en variables de RAM; no hay persistencia en flash (NVS/SPIFFS/Preferences) para ningún valor. Tras un reinicio, el coche siempre vuelve a: motor detenido, ambos servos centrados en 90°, servo auxiliar en modo manual (`controlServoAuxHabilitado = true` es el valor inicial). Las credenciales de red y la IP fija sí sobreviven porque están grabadas en el propio firmware, no porque se persistan en tiempo de ejecución.
- **El punto de acceso WiFi se cae:** un vigilante en `loop()` revisa cada 3000 ms si el modo WiFi sigue siendo AP y si el AP sigue iniciado; si no, lo reinicia automáticamente (ver [Tareas automáticas de fondo](#tareas-automáticas-de-fondo)). Esto es autónomo por parte del ESP32, pero el dispositivo cliente no se reconecta solo: hay que volver a unirse a la red WiFi manualmente desde el teléfono/tablet.
- **El cliente pierde el rango de la señal o cierra la pestaña del navegador mientras el coche está en movimiento:** **no existe ningún temporizador de seguridad (failsafe/deadman) en el firmware.** El motor y los servos mantienen el último valor recibido indefinidamente hasta que llegue un nuevo comando HTTP. Recuperar el control requiere reconectar el cliente y enviar manualmente una orden de parada (o cortar la alimentación física del motor).
- **Segundo dispositivo intenta conectarse mientras ya hay un cliente activo:** rechazado a nivel de WiFi (`max_connection=1` en `WiFi.softAP`), no llega a interactuar con el servidor HTTP.
- **Movimientos de slider muy pequeños (menos de 2°) en dirección o servo auxiliar:** se ignoran silenciosamente (el objetivo no cambia) aunque la petición HTTP se procese y responda `OK`; es un margen de histéresis para evitar micro-jitter del servo, no un fallo.
- **Ruta desconocida o parámetro faltante:** respuesta HTTP explícita (`404` o `400` con mensaje), nunca un cuelgue silencioso del servidor.

## Tareas automáticas de fondo

El firmware ejecuta dos procesos continuos en segundo plano dentro de `loop()`. En ambos casos, la condición que dispara la revisión es distinta de la condición que produce el efecto real — esta distinción importa para depurar por qué "a veces no pasa nada visible":

### Vigilancia del punto de acceso (`vigilarAccessPoint`)

- **Se ejecuta** en cada vuelta de `loop()`, pero solo actúa cada 3000 ms (`intervaloChequeoAPMs`).
- **Solo reinicia el AP** (`iniciarAccessPoint()`) si en ese chequeo detecta que el modo WiFi no es `WIFI_MODE_AP` o que la bandera interna `apIniciado` está en falso. Si el AP está sano, el chequeo no tiene ningún efecto observable.

### Oscilación automática del servo auxiliar

- **Se ejecuta** en cada vuelta de `loop()` (dentro de `actualizarServoAuxSuave`), pero solo avanza un paso cada 15 ms (`intervaloServoAuxMs`).
- **Solo mueve el servo** cuando el modo automático está activo (`controlServoAuxHabilitado == false`, activado desde la interfaz marcando la casilla "Giro automatico", que internamente envía `/servoauxmodo?enabled=0`). En modo manual, este bloque de código ni se evalúa.
- Cuando está activo, el servo barre continuamente de 0° a 180° y de vuelta, en pasos de 2°, sin intervención del usuario, hasta que se desactive el modo automático.

## Solución de problemas

### Red WiFi / punto de acceso

| Síntoma | Causas posibles | Solución |
|---|---|---|
| La red `COCHERC_B` no aparece | El AP tardó en levantarse tras el arranque, o se reinició justo en ese instante (revisión cada 3 s) | Esperar unos segundos y volver a escanear redes; si persiste, revisar alimentación del ESP32 |
| No se puede conectar aunque la red es visible | Ya hay otro cliente conectado (límite de 1 dispositivo) | Desconectar el otro dispositivo de la red antes de intentar de nuevo |
| El coche deja de responder tras un rato sin usarlo | No hay reconexión automática del lado del cliente ni failsafe de motor; puede que el teléfono se haya desconectado de la red | Volver a conectar el dispositivo manualmente a `COCHERC_B` y recargar `http://192.168.4.5/` |

### Motor DC / puente H

| Síntoma | Causas posibles | Solución |
|---|---|---|
| El motor no gira nunca | Se llamó a `/command?cmd=avanzar` sin haber fijado antes una velocidad (PWM queda en 0); o cableado de `In2`/`In3`/`ENB` incorrecto | Usar los controles de la interfaz web (usan `/motor`, que fija sentido y velocidad juntos) en vez de `/command` a mano; verificar GPIO 25/33/32 |
| El motor gira siempre igual de rápido sin importar el slider | Los valores por debajo de 85 en `/velocidad` se acotan a 85 (no hay velocidad "lenta" por debajo de ese piso) | Comportamiento esperado del rango 85–230; no es un fallo |
| El motor se calienta o no responde a "parar" | Revisar que ningún cliente externo esté enviando `/motor` o `/command` repetidamente con valores distintos de parar | Enviar explícitamente `/command?cmd=parar` o `/motor?value=0` |

### Servo de dirección

| Síntoma | Causas posibles | Solución |
|---|---|---|
| No se mueve al tocar el slider | Movimiento menor a 2° respecto al objetivo actual (se ignora por diseño), o límite de tasa de 25 ms muy seguido | Mover el slider un tramo más amplio |
| Se mueve en el sentido contrario al esperado respecto al gesto del dedo | El frontend invierte el valor antes de enviarlo (`180 - v`) para compensar el montaje físico del servo; si se remonta el brazo del servo en sentido opuesto, este es el punto exacto del código a ajustar | Ver nota en [CODIGO.md](CODIGO.md#decisiones-de-diseño-no-obvias) |
| Vibra o hace ruido en reposo | Comportamiento normal solo si nunca llega a estar quieto 300 ms seguidos; si vibra estando aparentemente detenido, revisar alimentación del servo | Verificar tensión/corriente de la fuente del servo |

### Servo auxiliar / modo automático

| Síntoma | Causas posibles | Solución |
|---|---|---|
| Empieza a girar solo sin que se haya tocado nada | Se activó el modo automático (casilla "Giro automatico" marcada) | Desmarcar la casilla para volver a modo manual |
| El slider auxiliar no responde | El modo automático está activo; `/servoaux` se ignora mientras tanto | Desmarcar "Giro automatico" antes de mover el slider |

### Interfaz web

| Síntoma | Causas posibles | Solución |
|---|---|---|
| La página no carga | El dispositivo no está conectado a `COCHERC_B`, o se usó una IP distinta de `192.168.4.5` | Verificar conexión WiFi y usar exactamente `http://192.168.4.5/` |
| Los controles responden con retraso perceptible | Throttle de 80 ms en el frontend + límites de tasa del backend (25–40 ms) sumados a la velocidad de movimiento del servo (2°/15 ms) | Comportamiento esperado por diseño, no es un fallo de red |
