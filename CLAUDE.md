# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**CarritoRC** is an ESP32-based RC car controller. The ESP32 runs as a WiFi Access Point and hosts a web server that serves a mobile-friendly control interface. There is a single source file: [src/main.cpp](src/main.cpp).

## Build & Flash Commands (PlatformIO CLI)

```bash
# Build only
pio run

# Build and upload to ESP32
pio run --target upload

# Open serial monitor (115200 baud)
pio device monitor

# Build, upload, then monitor
pio run --target upload; pio device monitor

# Clean build artifacts
pio run --target clean
```

> PlatformIO must be installed. In VS Code the PlatformIO IDE extension handles this automatically. On bare CLI: `pip install platformio`.

## Hardware & Pin Map

| Signal | GPIO | Notes |
|--------|------|-------|
| `In2`  | 25   | H-bridge direction pin 1 |
| `In3`  | 33   | H-bridge direction pin 2 |
| `ENB`  | 32   | H-bridge PWM enable (LEDC channel 0, 10 kHz, 8-bit) |
| `servoPin` | 13 | Main steering servo |
| `servoAuxPin` | 27 | Auxiliary servo |

Motor driver assumed: L298N-style H-bridge. Both `In2`/`In3` LOW = stop.

## WiFi / Network

- Mode: **Access Point only** (WIFI_AP)
- SSID: `COCHERC_B` / Password: `87654321` / Channel: 2
- Fixed AP IP: `192.168.4.5` (gateway `192.168.4.1`)
- Max 1 connected client (`WiFi.softAP(..., 0, 1)`)
- A watchdog in `loop()` (`vigilarAccessPoint`) reinitializes the AP every 3 s if it goes down

## HTTP API

The web server runs on port 80. All endpoints are GET requests with query parameters.

| Endpoint | Params | Description |
|----------|--------|-------------|
| `/` | — | Serves the HTML control page (stored in PROGMEM) |
| `/command` | `cmd=avanzar\|reversa\|parar` | Motor direction / stop |
| `/velocidad` | `value=85..230` | Motor PWM duty cycle (steps of 5) |
| `/direccion` | `value=20..160` | Steering servo target angle (steps of 5) |
| `/servoaux` | `value=20..160` | Auxiliary servo target angle (steps of 5) |

All endpoints return `200 text/plain "OK"` on success. The backend enforces its own rate limits (25 ms for direction/servoaux, 40 ms for velocity) independently of the frontend throttle (80 ms).

## Architecture

Everything lives in `src/main.cpp`. The main design patterns to know:

### Servo smooth interpolation
Both servos use the same pattern: `anguloObjetivo` is set by the HTTP handler; `actualizarServoSuave()` / `actualizarServoAuxSuave()` in `loop()` advance the actual angle 2° every 15 ms toward the target. Servos auto-detach 300 ms after reaching the target to eliminate jitter/heat. They re-attach on the next movement request.

PWM timers 2 and 3 are pre-allocated for the ESP32Servo library (`ESP32PWM::allocateTimer`); LEDC channel 0 is reserved for the motor.

### Value rounding
Both frontend (JS) and backend (C++) round values to the nearest multiple of 5 (`redondearA5`). Always keep these in sync when changing slider ranges.

### Frontend
The HTML/CSS/JS control page is embedded as a `PROGMEM` raw string literal (`html[]`) in `main.cpp`. It uses `fetch()` with a `throttle()` helper to rate-limit slider events to 80 ms before they reach the server.

## Library Dependency

```ini
lib_deps =
    madhephaestus/ESP32Servo @ ^0.13.0
```

Managed by PlatformIO; cached under `.pio/libdeps/`. Do not edit files there.
